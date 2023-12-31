// Copyright (C) 2023 JiDe Zhang <zhangjide@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "wrenderhelper.h"
#include "wbackend.h"
#include "wtools.h"
#include "private/wqmlhelper_p.h"

#include <qwbackend.h>
#include <qwoutput.h>
#include <qwrenderer.h>
#include <qwswapchain.h>
#include <qwbuffer.h>
#include <qwtexture.h>
#include <qwbufferinterface.h>

#include <QSGTexture>
#include <private/qquickrendercontrol_p.h>
#include <private/qquickwindow_p.h>
#include <private/qrhi_p.h>
#include <private/qsgplaintexture_p.h>
#include <private/qsgadaptationlayer_p.h>
#include <private/qsgsoftwarepixmaptexture_p.h>

extern "C" {
#define static
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#undef static
#include <wlr/render/pixman.h>
#include <wlr/render/egl.h>
#include <wlr/render/pixman.h>
#ifdef ENABLE_VULKAN_RENDER
#include <wlr/render/vulkan.h>
#endif
#include <wlr/util/region.h>
#include <wlr/backend.h>
#include <wlr/backend/interface.h>
#include <wlr/types/wlr_buffer.h>
}

#include <drm_fourcc.h>
#include <dlfcn.h>

QW_USE_NAMESPACE
WAYLIB_SERVER_BEGIN_NAMESPACE

struct BufferData {
    BufferData() {

    }

    ~BufferData() {
        resetWindowRenderTarget();
    }

    QWBuffer *buffer = nullptr;
    // for software renderer
    WImageRenderTarget paintDevice;
    QQuickRenderTarget renderTarget;
    QQuickWindowRenderTarget windowRenderTarget;

    inline void resetWindowRenderTarget() {
        if (windowRenderTarget.owns) {
            delete windowRenderTarget.renderTarget;
            delete windowRenderTarget.rpDesc;
            delete windowRenderTarget.texture;
            delete windowRenderTarget.renderBuffer;
            delete windowRenderTarget.depthStencil;
            delete windowRenderTarget.paintDevice;
        }

        windowRenderTarget.renderTarget = nullptr;
        windowRenderTarget.rpDesc = nullptr;
        windowRenderTarget.texture = nullptr;
        windowRenderTarget.renderBuffer = nullptr;
        windowRenderTarget.depthStencil = nullptr;
        windowRenderTarget.paintDevice = nullptr;
        windowRenderTarget.owns = false;
    }
};

// Copy from qquickrendertarget.cpp
static bool createRhiRenderTarget(const QRhiColorAttachment &colorAttachment,
                                  const QSize &pixelSize,
                                  int sampleCount,
                                  QRhi *rhi,
                                  QQuickWindowRenderTarget &dst)
{
    std::unique_ptr<QRhiRenderBuffer> depthStencil(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, sampleCount));
    if (!depthStencil->create()) {
        qWarning("Failed to build depth-stencil buffer for QQuickRenderTarget");
        return false;
    }

    QRhiTextureRenderTargetDescription rtDesc(colorAttachment);
    rtDesc.setDepthStencilBuffer(depthStencil.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());

    if (!rt->create()) {
        qWarning("Failed to build texture render target for QQuickRenderTarget");
        return false;
    }

    dst.renderTarget = rt.release();
    dst.rpDesc = rp.release();
    dst.depthStencil = depthStencil.release();
    dst.owns = true; // ownership of the native resource itself is not transferred but the QRhi objects are on us now

    return true;
}

bool createRhiRenderTarget(QRhi *rhi, const QQuickRenderTarget &source, QQuickWindowRenderTarget &dst)
{
    auto rtd = QQuickRenderTargetPrivate::get(&source);

    switch (rtd->type) {
    case QQuickRenderTargetPrivate::Type::NativeTexture: {
        const auto format = rtd->u.nativeTexture.rhiFormat == QRhiTexture::UnknownFormat ? QRhiTexture::RGBA8
                                                                                         : QRhiTexture::Format(rtd->u.nativeTexture.rhiFormat);
        const auto flags = QRhiTexture::RenderTarget | QRhiTexture::Flags(rtd->u.nativeTexture.rhiFlags);
        std::unique_ptr<QRhiTexture> texture(rhi->newTexture(format, rtd->pixelSize, rtd->sampleCount, flags));
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        if (!texture->createFrom({ rtd->u.nativeTexture.object, rtd->u.nativeTexture.layout }))
#else
        if (!texture->createFrom({ rtd->u.nativeTexture.object, rtd->u.nativeTexture.layoutOrState }))
#endif
            return false;
        QRhiColorAttachment att(texture.get());
        if (!createRhiRenderTarget(att, rtd->pixelSize, rtd->sampleCount, rhi, dst))
            return false;
        dst.texture = texture.release();
        return true;
    }
    case QQuickRenderTargetPrivate::Type::NativeRenderbuffer: {
        std::unique_ptr<QRhiRenderBuffer> renderbuffer(rhi->newRenderBuffer(QRhiRenderBuffer::Color, rtd->pixelSize, rtd->sampleCount));
        if (!renderbuffer->createFrom({ rtd->u.nativeRenderbufferObject })) {
            qWarning("Failed to build wrapper renderbuffer for QQuickRenderTarget");
            return false;
        }
        QRhiColorAttachment att(renderbuffer.get());
        if (!createRhiRenderTarget(att, rtd->pixelSize, rtd->sampleCount, rhi, dst))
            return false;
        dst.renderBuffer = renderbuffer.release();
        return true;
    }

    default:
        break;
    }

    return false;
}
// Copy end

class WRenderHelperPrivate : public WObjectPrivate
{
public:
    WRenderHelperPrivate(WRenderHelper *qq, QWRenderer *renderer)
        : WObjectPrivate(qq)
        , renderer(renderer)
    {}

    void resetRenderBuffer();
    void onBufferDestroy(QWBuffer *buffer);
    static bool ensureRhiRenderTarget(QQuickRenderControl *rc, BufferData *data);

    W_DECLARE_PUBLIC(WRenderHelper)
    QWRenderer *renderer;
    QList<BufferData*> buffers;
    BufferData *lastBuffer = nullptr;

    QSize size;
};

void WRenderHelperPrivate::resetRenderBuffer()
{
    qDeleteAll(buffers);
    lastBuffer = nullptr;
    buffers.clear();
}

void WRenderHelperPrivate::onBufferDestroy(QWBuffer *buffer)
{
    for (int i = 0; i < buffers.count(); ++i) {
        auto data = buffers[i];
        if (data->buffer == buffer) {
            if (lastBuffer == data)
                lastBuffer = nullptr;
            buffers.removeAt(i);
            break;
        }
    }
}

bool WRenderHelperPrivate::ensureRhiRenderTarget(QQuickRenderControl *rc, BufferData *data)
{
    data->resetWindowRenderTarget();
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    auto rhi = QQuickRenderControlPrivate::get(rc)->rhi;
#else
    auto rhi = rc->rhi();
#endif
    auto tmp = data->renderTarget;
    bool ok = createRhiRenderTarget(rhi, tmp, data->windowRenderTarget);
    if (!ok)
        return false;
    data->renderTarget = QQuickRenderTarget::fromRhiRenderTarget(data->windowRenderTarget.renderTarget);
    data->renderTarget.setDevicePixelRatio(tmp.devicePixelRatio());
    data->renderTarget.setMirrorVertically(tmp.mirrorVertically());

    return true;
}

WRenderHelper::WRenderHelper(QWRenderer *renderer, QObject *parent)
    : QObject(parent)
    , WObject(*new WRenderHelperPrivate(this, renderer))
{

}

QSize WRenderHelper::size() const
{
    W_DC(WRenderHelper);
    return d->size;
}

void WRenderHelper::setSize(const QSize &size)
{
    W_D(WRenderHelper);
    if (d->size == size)
        return;
    d->size = size;
    d->resetRenderBuffer();

    Q_EMIT sizeChanged();
}

QSGRendererInterface::GraphicsApi WRenderHelper::getGraphicsApi(QQuickRenderControl *rc)
{
    auto d = QQuickRenderControlPrivate::get(rc);
    return d->sg->rendererInterface(d->rc)->graphicsApi();
}

QSGRendererInterface::GraphicsApi WRenderHelper::getGraphicsApi()
{
    auto getApi = [] () {
        // Only for get GraphicsApi
        QQuickRenderControl rc;
        return getGraphicsApi(&rc);
    };

    static auto api = getApi();
    return api;
}

class Q_DECL_HIDDEN GLTextureBuffer : public QWBufferInterface
{
public:
    explicit GLTextureBuffer(wlr_egl *egl, QSGTexture *texture);

    bool getDmabuf(wlr_dmabuf_attributes *attribs) const override;

private:
    wlr_egl *m_egl;
    QSGTexture *m_texture;
};

GLTextureBuffer::GLTextureBuffer(wlr_egl *egl, QSGTexture *texture)
    : m_egl(egl)
    , m_texture(texture)
{

}

bool GLTextureBuffer::getDmabuf(wlr_dmabuf_attributes *attribs) const
{
    auto rhiTexture = m_texture->rhiTexture();
    if (!rhiTexture)
        return false;

    auto display = wlr_egl_get_display(m_egl);
    auto context = wlr_egl_get_context(m_egl);

    EGLImage image = eglCreateImage(display, context,
                                    EGL_GL_TEXTURE_2D,
                                    reinterpret_cast<EGLClientBuffer>(rhiTexture->nativeTexture().object),
                                    nullptr);

    if (image == EGL_NO_IMAGE)
        return false;

    static auto eglExportDMABUFImageQueryMESA =
        reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC>(eglGetProcAddress("eglExportDMABUFImageQueryMESA"));
    static auto eglExportDMABUFImageMESA =
        reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEMESAPROC>(eglGetProcAddress("eglExportDMABUFImageMESA"));

    if (!eglExportDMABUFImageQueryMESA || !eglExportDMABUFImageMESA)
        return false;

    bool ok = eglExportDMABUFImageQueryMESA(display,
                                            image,
                                            reinterpret_cast<int*>(&attribs->format),
                                            &attribs->n_planes,
                                            &attribs->modifier);
    if (!ok)
        return false;

    ok = eglExportDMABUFImageMESA(display,
                                  image,
                                  attribs->fd,
                                  reinterpret_cast<int*>(attribs->stride),
                                  reinterpret_cast<int*>(attribs->offset));
    if (!ok)
        return false;

    attribs->width = handle()->width;
    attribs->height = handle()->height;

    return true;
}

#ifdef ENABLE_VULKAN_RENDER
class Q_DECL_HIDDEN VkTextureBuffer : public QWBufferInterface
{
public:
    explicit VkTextureBuffer(VkInstance instance, VkDevice device, QSGTexture *texture);

    bool getDmabuf(wlr_dmabuf_attributes *attribs) const override;

private:
    VkInstance m_instance;
    VkDevice m_device;
    QSGTexture *m_texture;
};

VkTextureBuffer::VkTextureBuffer(VkInstance instance, VkDevice device, QSGTexture *texture)
    : m_instance(instance)
    , m_device(device)
    , m_texture(texture)
{

}

bool VkTextureBuffer::getDmabuf(wlr_dmabuf_attributes *attribs) const
{
//    static auto vkGetInstanceProcAddr =
//        reinterpret_cast<PFN_vkGetInstanceProcAddr>(::dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr"));
//    static auto vkGetMemoryFdKHR =
//        reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetInstanceProcAddr(m_instance, "vkGetMemoryFdKHR"));
//    static auto vkGetImageMemoryRequirements =
//        reinterpret_cast<PFN_vkGetImageMemoryRequirements>(vkGetInstanceProcAddr(m_instance, "vkGetImageMemoryRequirements"));
//    static auto vkGetImageSparseMemoryRequirements =
//        reinterpret_cast<PFN_vkGetImageSparseMemoryRequirements>(vkGetInstanceProcAddr(m_instance, "vkGetImageSparseMemoryRequirements"));
//    static auto vkGetImageSubresourceLayout =
//        reinterpret_cast<PFN_vkGetImageSubresourceLayout>(vkGetInstanceProcAddr(m_instance, "vkGetImageSubresourceLayout"));

    // TODO
    return false;
}
#endif

class Q_DECL_HIDDEN QImageBuffer : public QWBufferInterface
{
public:
    explicit QImageBuffer(const QImage &image);

    bool getShm(wlr_shm_attributes *attribs) const override;
    bool beginDataPtrAccess(uint32_t flags, void **data, uint32_t *format, size_t *stride) override;
    void endDataPtrAccess();

private:
    QImage m_image;
};

QImageBuffer::QImageBuffer(const QImage &image)
    : m_image(image)
{

}

bool QImageBuffer::getShm(wlr_shm_attributes *attribs) const
{
    attribs->fd = 0;
    attribs->format = WTools::toDrmFormat(m_image.format());
    attribs->width = m_image.width();
    attribs->height = m_image.height();
    attribs->stride = m_image.bytesPerLine();
    return true;
}

bool QImageBuffer::beginDataPtrAccess(uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
    Q_UNUSED(flags);
    *data = m_image.bits();
    *format = WTools::toDrmFormat(m_image.format());
    *stride = m_image.bytesPerLine();

    return true;
}

void QImageBuffer::endDataPtrAccess()
{

}

QWBuffer *WRenderHelper::toBuffer(QWRenderer *renderer, QSGTexture *texture)
{
    const QSize size = texture->textureSize();

    switch (getGraphicsApi()) {
    case QSGRendererInterface::OpenGL: {
        Q_ASSERT(wlr_renderer_is_gles2(renderer->handle()));
        auto egl = wlr_gles2_renderer_get_egl(renderer->handle());
        return QWBuffer::create(new GLTextureBuffer(egl, texture), size.width(), size.height());
    }
#ifdef ENABLE_VULKAN_RENDER
    case QSGRendererInterface::Vulkan: {
        Q_ASSERT(wlr_renderer_is_vk(renderer->handle()));
        auto instance = wlr_vk_renderer_get_instance(renderer->handle());
        auto device = wlr_vk_renderer_get_device(renderer->handle());
        return QWBuffer::create(new VkTextureBuffer(instance, device, texture), size.width(), size.height());
    }
#endif
    case QSGRendererInterface::Software: {
        QImage image;
        if (auto t = qobject_cast<QSGPlainTexture*>(texture)) {
            image = t->image();
        } else if (auto t = qobject_cast<QSGLayer*>(texture)) {
            image = t->toImage();
        } else if (QByteArrayView(texture->metaObject()->className())
                   == QByteArrayView("QSGSoftwarePixmapTexture")) {
            auto t = static_cast<QSGSoftwarePixmapTexture*>(texture);
            image = t->pixmap().toImage();
        } else {
            qFatal("Can't get QImage from QSGTexture, class name: %s", texture->metaObject()->className());
        }

        if (image.isNull())
            return nullptr;

        return QWBuffer::create(new QImageBuffer(image), image.width(), image.height());
    }
    default:
        qFatal("Can't get QWBuffer from QSGTexture, Not supported graphics API.");
        break;
    }

    return nullptr;
}

QQuickRenderTarget WRenderHelper::acquireRenderTarget(QQuickRenderControl *rc, QWBuffer *buffer)
{
    W_D(WRenderHelper);
    Q_ASSERT(buffer);

    if (d->size.isEmpty())
        return {};

    for (int i = 0; i < d->buffers.count(); ++i) {
        auto data = d->buffers[i];
        if (data->buffer == buffer) {
            d->lastBuffer = data;
            return data->renderTarget;
        }
    }

    std::unique_ptr<BufferData> bufferData(new BufferData);
    bufferData->buffer = buffer;
    auto texture = QWTexture::fromBuffer(d->renderer, buffer);

    QQuickRenderTarget rt;

    if (wlr_renderer_is_pixman(d->renderer->handle())) {
        pixman_image_t *image = wlr_pixman_texture_get_image(texture->handle());
        void *data = pixman_image_get_data(image);
        if (bufferData->paintDevice.constBits() != data)
            bufferData->paintDevice = WTools::fromPixmanImage(image, data);
        Q_ASSERT(!bufferData->paintDevice.isNull());
        rt = QQuickRenderTarget::fromPaintDevice(&bufferData->paintDevice);
    }
#ifdef ENABLE_VULKAN_RENDER
    else if (wlr_renderer_is_vk(d->renderer->handle())) {
        wlr_vk_image_attribs attribs;
        wlr_vk_texture_get_image_attribs(texture->handle(), &attribs);
        rt = QQuickRenderTarget::fromVulkanImage(attribs.image, attribs.layout, attribs.format, d->size);
    }
#endif
    else if (wlr_renderer_is_gles2(d->renderer->handle())) {
        wlr_gles2_texture_attribs attribs;
        wlr_gles2_texture_get_attribs(texture->handle(), &attribs);

        rt = QQuickRenderTarget::fromOpenGLTexture(attribs.tex, d->size);
        rt.setMirrorVertically(true);
    }

    delete texture;
    bufferData->renderTarget = rt;

    if (QSGRendererInterface::isApiRhiBased(getGraphicsApi(rc))) {
        if (!rt.isNull()) {
            // Force convert to Rhi render target
            if (!d->ensureRhiRenderTarget(rc, bufferData.get()))
                bufferData->renderTarget = {};
        }

        if (bufferData->renderTarget.isNull())
            return {};
    }

    connect(buffer, SIGNAL(beforeDestroy(QWBuffer*)),
            this, SLOT(onBufferDestroy(QWBuffer*)), Qt::UniqueConnection);

    d->buffers.append(bufferData.release());
    d->lastBuffer = d->buffers.last();

    return d->buffers.last()->renderTarget;
}

std::pair<QWBuffer *, QQuickRenderTarget> WRenderHelper::lastRenderTarget() const
{
    W_DC(WRenderHelper);
    if (!d->lastBuffer)
        return {nullptr, {}};

    return {d->lastBuffer->buffer, d->lastBuffer->renderTarget};
}

static QWRenderer *createRendererWithType(const char *type, QWBackend *backend)
{
    qputenv("WLR_RENDERER", type);
    auto render = QWRenderer::autoCreate(backend);
    qunsetenv("WLR_RENDERER");

    return render;
}

QWRenderer *WRenderHelper::createRenderer(QWBackend *backend)
{
    QWRenderer *renderer = nullptr;
    auto api = getGraphicsApi();

    switch (api) {
    case QSGRendererInterface::OpenGL:
        renderer = createRendererWithType("gles2", backend);
        Q_ASSERT(!renderer || wlr_renderer_is_gles2(renderer->handle()));
        break;
#ifdef ENABLE_VULKAN_RENDER
    case QSGRendererInterface::Vulkan: {
        renderer = createRendererWithType("vulkan", backend);
        Q_ASSERT(!renderer || wlr_renderer_is_vk(renderer->handle()));
        break;
    }
#endif
    case QSGRendererInterface::Software:
        renderer = createRendererWithType("pixman", backend);
        Q_ASSERT(!renderer || wlr_renderer_is_pixman(renderer->handle()));
        break;
    default:
        qFatal("Not supported graphics api: %s", qPrintable(QQuickWindow::sceneGraphBackend()));
        break;
    }

    return renderer;
}

WAYLIB_SERVER_END_NAMESPACE

#include "moc_wrenderhelper.cpp"
