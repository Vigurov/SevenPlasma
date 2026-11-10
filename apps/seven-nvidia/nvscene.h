#pragma once
/*
 * The preview of "Adjust image settings with preview", from NVIDIA's own scene in nvGameS.dll (driver 347.88):
 * the logo mesh (resource X/1000, converted by tools/x2mesh.py into logo-mesh.bin), the sky (RCDATA/1001)
 * and the concrete floor (RCDATA/1002). Material as in the mesh: diffuse and specular #76B900, power 12.8.
 * Quality as on Windows: Performance without antialiasing or texture mipmaps, Quality with MSAA and
 * anisotropic filtering.
 */
#include <QFile>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QStandardPaths>
#include <QVector3D>
#include <memory>

namespace Nv
{
class Scene : protected QOpenGLExtraFunctions
{
public:
    static QString asset(const QString &name)
    {
        return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-nvidia/") + name);
    }

    void init()
    {
        initializeOpenGLFunctions();
        QFile f(asset(QStringLiteral("logo-mesh.bin")));
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            m_corners = data.size() / (6 * 4);
            m_logo.create();
            m_logo.bind();
            m_logo.allocate(data.constData(), data.size());
            m_logo.release();
        }
        const float floorQuad[] = {-600, 0, -600, 600, 0, -600, 600, 0, 600, -600, 0, -600, 600, 0, 600, -600, 0, 600};
        m_floor.create();
        m_floor.bind();
        m_floor.allocate(floorQuad, sizeof(floorQuad));
        m_floor.release();
        const float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
        m_quad.create();
        m_quad.bind();
        m_quad.allocate(quad, sizeof(quad));
        m_quad.release();

        m_skyTex = std::make_unique<QOpenGLTexture>(QImage(asset(QStringLiteral("scene-sky.jpg"))).mirrored());
        m_skyTex->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        m_skyTex->setWrapMode(QOpenGLTexture::ClampToEdge);
        m_floorTex = std::make_unique<QOpenGLTexture>(QImage(asset(QStringLiteral("scene-floor.jpg"))).mirrored());
        m_floorTex->setWrapMode(QOpenGLTexture::Repeat);

        m_logoProg.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 120\n"
                                           "attribute vec3 pos; attribute vec3 nrm; uniform mat4 mvp; uniform mat4 model;"
                                           "varying vec3 vN; varying vec3 vP;"
                                           "void main() { vN = mat3(model) * nrm; vP = (model * vec4(pos, 1.0)).xyz; gl_Position = mvp * vec4(pos, 1.0); }");
        m_logoProg.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 120\n"
                                           "varying vec3 vN; varying vec3 vP; uniform vec3 eye;"
                                           "void main() {"
                                           "  vec3 n = normalize(vN); vec3 l = normalize(vec3(-0.05, 0.12, 1.0)); vec3 v = normalize(eye - vP);"
                                           "  if (dot(n, v) < 0.0) n = -n;"
                                           "  vec3 green = vec3(0.463, 0.725, 0.0);"
                                           "  float d = max(dot(n, l), 0.0);"
                                           // measured on Windows: the face (106,166,0), the sides in shadow (36,56,0)
                                           "  gl_FragColor = vec4(green * (0.30 + 0.60 * d), 1.0);"
                                           "}");
        m_logoProg.bindAttributeLocation("pos", 0);
        m_logoProg.bindAttributeLocation("nrm", 1);
        m_logoProg.link();

        m_floorProg.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                            "#version 120\n"
                                            "attribute vec3 pos; uniform mat4 mvp; uniform float tile; varying vec2 uv; varying float dist; uniform vec3 eye;"
                                            "void main() { uv = pos.xz / tile; dist = length(pos - eye); gl_Position = mvp * vec4(pos, 1.0); }");
        m_floorProg.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                            "#version 120\n"
                                            "uniform sampler2D tex; varying vec2 uv; varying float dist; uniform vec3 haze;"
                                            "void main() { vec3 c = texture2D(tex, uv).rgb * 0.97;"
                                            "  float f = clamp((dist - 300.0) / 1200.0, 0.0, 1.0);"
                                            "  gl_FragColor = vec4(mix(c, haze, f * 0.55), 1.0); }");
        m_floorProg.bindAttributeLocation("pos", 0);
        m_floorProg.link();

        m_skyProg.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                          "#version 120\n"
                                          "attribute vec2 pos; uniform float horizon; varying vec2 uv;"
                                          "void main() { uv = vec2(pos.x * 0.5 + 0.5, 0.5 * (pos.y - horizon) / (1.0 - horizon)); gl_Position = vec4(pos, 0.999, 1.0); }");
        m_skyProg.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                          "#version 120\n"
                                          "uniform sampler2D tex; varying vec2 uv; uniform vec3 haze;"
                                          "void main() { if (uv.y < 0.0) { gl_FragColor = vec4(haze, 1.0); return; }"
                                          "  gl_FragColor = vec4(texture2D(tex, vec2(uv.x * 0.8 + 0.1, uv.y)).rgb, 1.0); }");
        m_skyProg.bindAttributeLocation("pos", 0);
        m_skyProg.link();
        m_ready = true;
    }

    /** Draws into the framebuffer that is bound now (quality 0 performance, 1 balanced, 2 quality) */
    void render(int w, int h, double angle, int quality, GLuint target)
    {
        if (!m_ready) {
            return;
        }
        const int samples = quality == 0 ? 0 : (quality == 1 ? 4 : 8);
        if (!m_fbo || m_fbo->size() != QSize(w, h) || m_samples != samples) {
            QOpenGLFramebufferObjectFormat fmt;
            fmt.setAttachment(QOpenGLFramebufferObject::Depth);
            fmt.setSamples(samples);
            m_fbo = std::make_unique<QOpenGLFramebufferObject>(QSize(w, h), fmt);
            m_samples = samples;
        }
        if (quality == 0) {
            m_floorTex->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_floorTex->setMaximumAnisotropy(1);
        } else {
            m_floorTex->generateMipMaps();
            m_floorTex->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
            m_floorTex->setMaximumAnisotropy(quality == 2 ? 16 : 4);
        }
        m_fbo->bind();
        glViewport(0, 0, w, h);
        const QVector3D haze(0.80, 0.86, 0.90);
        glClearColor(haze.x(), haze.y(), haze.z(), 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // camera: a little below the logo's centre, looking at it; the logo stands on the floor
        const QVector3D eye(0, eyeY, eyeZ);
        QMatrix4x4 proj, view;
        proj.perspective(fov, float(w) / h, 1.0f, 2000.0f);
        view.lookAt(eye, QVector3D(0, targetY, 0), QVector3D(0, 1, 0));
        const QMatrix4x4 vp = proj * view;
        // where the horizon falls on the screen (a point far ahead at eye height)
        const QVector4D far = vp * QVector4D(0, eye.y(), -100000, 1);
        const float horizon = far.y() / far.w();

        glDisable(GL_DEPTH_TEST);
        m_skyProg.bind();
        m_skyProg.setUniformValue("horizon", horizon);
        m_skyProg.setUniformValue("haze", haze);
        m_skyProg.setUniformValue("tex", 0);
        m_skyTex->bind(0);
        m_quad.bind();
        m_skyProg.enableAttributeArray(0);
        m_skyProg.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_skyProg.disableAttributeArray(0);
        m_quad.release();

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        QMatrix4x4 floorModel;
        floorModel.translate(0, floorY, 0);
        m_floorProg.bind();
        m_floorProg.setUniformValue("mvp", vp * floorModel);
        m_floorProg.setUniformValue("tile", tileSize);
        m_floorProg.setUniformValue("eye", eye - QVector3D(0, floorY, 0));
        m_floorProg.setUniformValue("haze", haze);
        m_floorProg.setUniformValue("tex", 0);
        m_floorTex->bind(0);
        m_floor.bind();
        m_floorProg.enableAttributeArray(0);
        m_floorProg.setAttributeBuffer(0, GL_FLOAT, 0, 3);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_floorProg.disableAttributeArray(0);
        m_floor.release();

        QMatrix4x4 model;
        model.rotate(angle, 0, 1, 0);
        m_logoProg.bind();
        m_logoProg.setUniformValue("mvp", vp * model);
        m_logoProg.setUniformValue("model", model);
        m_logoProg.setUniformValue("eye", eye);
        m_logo.bind();
        m_logoProg.enableAttributeArray(0);
        m_logoProg.enableAttributeArray(1);
        m_logoProg.setAttributeBuffer(0, GL_FLOAT, 0, 3, 24);
        m_logoProg.setAttributeBuffer(1, GL_FLOAT, 12, 3, 24);
        glDisable(GL_CULL_FACE);
        glDrawArrays(GL_TRIANGLES, 0, m_corners);
        m_logoProg.disableAttributeArray(0);
        m_logoProg.disableAttributeArray(1);
        m_logo.release();
        m_fbo->release();

        // resolve the multisampled picture into the target
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo->handle());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
    }

    // the camera, tuned against the Windows screenshots
    float eyeY = 1.85f, eyeZ = 53.0f, targetY = 3.95f, fov = 40.0f, floorY = -12.3f, tileSize = 220.0f;

private:
    bool m_ready = false;
    int m_corners = 0;
    int m_samples = -1;
    QOpenGLBuffer m_logo, m_floor, m_quad;
    std::unique_ptr<QOpenGLTexture> m_skyTex, m_floorTex;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    QOpenGLShaderProgram m_logoProg, m_floorProg, m_skyProg;
};
}
