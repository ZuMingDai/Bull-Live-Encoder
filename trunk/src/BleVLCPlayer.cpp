#include "BleVLCPlayer.hpp"

#include <QDir>

#include "BleLog.hpp"
#include "BleErrno.hpp"
#include "BleUtil.hpp"

#include "vlc/plugins/vlc_fourcc.h"

#define DEFAULT_WIDTH   1024
#define DEFAULT_HEIGHT  768

static void BleImageCleanupHandler(void *info)
{
    log_trace("---------------");
    char *data = (char*)info;
    delete [] data;
}

BleVLCPlayer::BleVLCPlayer(QObject *parent) :
    QObject(parent)
  , m_type(MEDIA_TYPE_INVALID)
  , m_width(DEFAULT_WIDTH)
  , m_height(DEFAULT_HEIGHT)
{

}

void BleVLCPlayer::setMediaType(int type)
{
    m_type = type;
}

void BleVLCPlayer::setMRL(const QString &mrl)
{
    m_mrl = mrl;
}

void BleVLCPlayer::setOptions(const QStringList &options)
{
    m_options = options;
}

int BleVLCPlayer::start()
{
    if (m_mrl.isEmpty()) {
        log_error("start error(url is empty)");
        return BLE_VLC_MEDIA_MRL_EMPTY_ERROR;
    }

    if (m_type <= MEDIA_TYPE_INVALID || m_type >= MEDIA_TYPE_NUMBER) {
        log_error("start error(media type is error)");
        return BLE_VLC_MEDIA_TYPE_ERROR;
    }

    /*!
        \param argc the number of arguments (should be 0)
        \param argv list of arguments (should be NULL)
    */
    m_VLCInstance = libvlc_new(0, NULL);
    if (!m_VLCInstance) {
        log_error("libvlc_new error.");
        return BLE_VLC_INTERNAL_ERROR;
    }

    m_VLCPlayer = libvlc_media_player_new(m_VLCInstance);
    if (!m_VLCPlayer) {
        log_error("libvlc_media_player_new error.");
        libvlc_release(m_VLCInstance);
        return BLE_VLC_INTERNAL_ERROR;
    }

    if (m_type == MEDIA_TYPE_FILE) {
        const char *fileName = QDir::toNativeSeparators(m_mrl).toStdString().c_str();
        m_VLCMedia = libvlc_media_new_path(m_VLCInstance, fileName);
    } else if (m_type == MEDIA_TYPE_NETSTREAM) {
        const char *url = m_mrl.toStdString().c_str();
        m_VLCMedia = libvlc_media_new_location(m_VLCInstance, url);
//        libvlc_media_add_option(vlc_media, ":screen-fps=24");
//        libvlc_media_add_option(vlc_media, ":screen-top=0");
//        libvlc_media_add_option(vlc_media, ":screen-left=0");
//        libvlc_media_add_option(vlc_media, ":screen-width=800");
//        libvlc_media_add_option(vlc_media, ":screen-height=600");
//        libvlc_media_add_option(vlc_media, ":screen-follow-mouse");

//        vlc_media = libvlc_media_new_location(vlc_instance, "dshow:// ");
//        libvlc_media_add_option(vlc_media, ":dshow-vdev=6RoomsCamV9");
//        libvlc_media_add_option(vlc_media, ":dshow-fps=15");
//        libvlc_media_add_option(vlc_media, ":screen-left=0");
//        libvlc_media_add_option(vlc_media, ":screen-width=800");
//        libvlc_media_add_option(vlc_media, ":screen-height=600");
//        libvlc_media_add_option(vlc_media, ":screen-follow-mouse");
    }

    foreach (const QString &option, m_options) {
        libvlc_media_add_option(m_VLCMedia, option.toStdString().c_str());
    }

    if (!m_VLCMedia) {
        libvlc_media_player_release(m_VLCPlayer);
        libvlc_release(m_VLCInstance);
        return BLE_VLC_MEDIA_OPEN_ERROR;
    }

    libvlc_media_player_set_media(m_VLCPlayer, m_VLCMedia);

    // parse and get size
    libvlc_media_parse(m_VLCMedia);
    if (libvlc_video_get_size(m_VLCPlayer, 0, &m_width, &m_height) != 0) {
        log_warn("libvlc_video_get_size error. use default size %dx%d", DEFAULT_WIDTH, DEFAULT_HEIGHT);
        m_width = DEFAULT_WIDTH;
        m_height = DEFAULT_HEIGHT;
    }

    // @note RV24 equal to RGB24
    // param pitch bytes count per line.
    // chroma a four-characters string identifying the chroma
    // (e.g. "RV32" or "YUYV")
    // see vlc_fourcc.h
    // also see www.fourcc.org for detail four-characters color code.
    libvlc_video_set_format(m_VLCPlayer, "RV24", m_width, m_height, m_width*3);
    libvlc_video_set_callbacks(m_VLCPlayer, vlc_video_lock_cb, NULL, vlc_video_display_cb, this);

    libvlc_media_player_play(m_VLCPlayer);

    return BLE_SUCESS;
}

void BleVLCPlayer::stop()
{
    if (libvlc_media_player_is_playing(m_VLCPlayer)) {
        libvlc_media_player_stop(m_VLCPlayer);
    }

    libvlc_media_release(m_VLCMedia);
    libvlc_media_player_release(m_VLCPlayer);
    libvlc_release(m_VLCInstance);
}

void BleVLCPlayer::pause()
{
    void libvlc_media_player_pause ( libvlc_media_player_t *p_mi );
    libvlc_media_player_pause(m_VLCPlayer);
}

QImage BleVLCPlayer::getImage()
{
    BleAutoLocker(m_modifyMutex);
    return m_image;
}

void BleVLCPlayer::addImage(QImage &image)
{
    BleAutoLocker(m_modifyMutex);
    m_image = image;
}

void BleVLCPlayer::vlc_video_display_cb(void *opaque, void *picture)
{
    BleVLCPlayer *source = (BleVLCPlayer *)opaque;
    int width = source->m_width;
    int height = source->m_height;

    QImage image(width, height, QImage::Format_RGB888);
    uchar *data = image.bits();
    memcpy(data, picture, image.byteCount());
    image = image.rgbSwapped();

    source->addImage(image);

    delete [] picture;
}

void *BleVLCPlayer::vlc_video_lock_cb(void *opaque, void **planes)
{
    BleVLCPlayer *source = (BleVLCPlayer *)opaque;
    int width = source->m_width;
    int height = source->m_height;

    int bytes = height * width * 3;
    char *pic_buffer = new char[bytes];;
    planes[0] = pic_buffer;

    return pic_buffer;
}
