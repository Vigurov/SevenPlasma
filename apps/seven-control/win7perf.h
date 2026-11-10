/*
 * Windows 7 "Performance Information and Tools" (PerfCenterCPL.dll): the Windows Experience Index
 * page, "Advanced Tools" and the detailed performance and system information.
 *
 * WinSAT does not exist here, so the subscores come from our own short tests, mapped onto the
 * Windows 7 scale (1.0 - 7.9):
 *   processor      -> zlib compression on every logical processor (MB/s)
 *   memory         -> memcpy bandwidth (GB/s)
 *   graphics       -> OpenGL fill rate of blended full-screen layers (Gpixel/s), like Aero
 *   gaming         -> OpenGL triangle throughput (Mtriangle/s)
 *   primary disk   -> sequential and 4K random reads of a test file on the system disk
 * Like WinSAT every test runs several times and the best pass counts.
 * Scores are kept in ~/.config/seven-weirc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7display.h"
#include "win7programs.h"

#include <QDateTime>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QPainterPath>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QProgressBar>
#include <QScreen>
#include <QSettings>
#include <QStorageInfo>

#include <atomic>
#include <cmath>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

namespace Win7Perf
{
enum {
    PerfView = 39,
    ToolsView = 43,
    DetailsView = 49,
};

inline bool isPerfView(int v)
{
    return v == PerfView || v == ToolsView || v == DetailsView;
}

inline QString viewTitle(int v)
{
    return v == ToolsView ? tr7("Advanced Tools") : v == DetailsView ? tr7("Performance Information and System Details")
                                                                                     : tr7("Performance Information and Tools");
}

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
};

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-weirc"), QSettings::IniFormat);
    return s;
}

/* ---------------- scoring ---------------- */

/** piecewise log interpolation of a measurement onto 1.0 - 7.9 */
inline double scale(double value, const QList<QPair<double, double>> &points)
{
    if (value <= 0) {
        return 1.0;
    }
    if (value <= points.first().first) {
        return qMax(1.0, points.first().second * value / points.first().first);
    }
    for (int i = 1; i < points.size(); ++i) {
        if (value <= points.at(i).first) {
            const double t = (std::log(value) - std::log(points.at(i - 1).first)) / (std::log(points.at(i).first) - std::log(points.at(i - 1).first));
            return points.at(i - 1).second + t * (points.at(i).second - points.at(i - 1).second);
        }
    }
    return 7.9;
}

inline double round1(double v)
{
    return std::floor(qBound(1.0, v, 7.9) * 10) / 10; // Windows truncates
}

inline double cpuTest()
{
    const int threads = qMax(1, int(std::thread::hardware_concurrency()));
    QByteArray data(4 * 1024 * 1024, Qt::Uninitialized);
    quint32 x = 12345;
    for (int i = 0; i < data.size(); ++i) {
        x = x * 1103515245 + 12345;
        data[i] = char((x >> 16) & 0x3f) + 'A'; // compressible but not trivial
    }
    std::atomic<qint64> bytes{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            while (!stop) {
                qCompress(data, 6);
                bytes += data.size();
            }
        });
    }
    QElapsedTimer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    stop = true;
    for (auto &t : pool) {
        t.join();
    }
    return bytes / 1048576.0 / (timer.elapsed() / 1000.0); // MB/s
}

inline double memoryTest()
{
    const size_t n = 128u * 1024 * 1024;
    std::vector<char> a(n, 1), b(n, 2);
    QElapsedTimer timer;
    timer.start();
    qint64 moved = 0;
    while (timer.elapsed() < 1500) {
        memcpy(b.data(), a.data(), n);
        moved += qint64(n) * 2; // read + write
    }
    return moved / 1e9 / (timer.elapsed() / 1000.0); // GB/s
}

/** sequential MB/s and random 4K IOPS on the system disk (the file lives in ~/.cache) */
inline QPair<double, double> diskTest()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/seven-wei-disk.tmp");
    const QByteArray p = QFile::encodeName(path);
    const size_t block = 4u * 1024 * 1024;
    const int blocks = 64; // 256 MB
    void *buf = nullptr;
    if (posix_memalign(&buf, 4096, block) != 0) {
        return {0, 0};
    }
    memset(buf, 0x5a, block);
    int fd = ::open(p.constData(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        free(buf);
        return {0, 0};
    }
    for (int i = 0; i < blocks; ++i) {
        if (::write(fd, buf, block) != ssize_t(block)) {
            break;
        }
    }
    fdatasync(fd);
    ::close(fd);
    double seq = 0, iops = 0;
    fd = ::open(p.constData(), O_RDONLY | O_DIRECT);
    if (fd >= 0) {
        QElapsedTimer t;
        t.start();
        qint64 got = 0;
        for (int i = 0; i < blocks; ++i) {
            const ssize_t r = ::pread(fd, buf, block, off_t(i) * block);
            if (r <= 0) {
                break;
            }
            got += r;
        }
        seq = got / 1048576.0 / qMax(0.001, t.elapsed() / 1000.0);
        t.restart();
        quint32 x = 777;
        int reads = 0;
        while (t.elapsed() < 1500) {
            x = x * 1664525 + 1013904223;
            const off_t off = off_t(x % (blocks * (block / 4096))) * 4096;
            if (::pread(fd, buf, 4096, off) != 4096) {
                break;
            }
            ++reads;
        }
        iops = reads / qMax(0.001, t.elapsed() / 1000.0);
        ::close(fd);
    }
    free(buf);
    QFile::remove(path);
    return {seq, iops};
}

/** {fill rate Gpix/s, Mtriangles/s, renderer} with an offscreen OpenGL context (GUI thread) */
inline std::tuple<double, double, QString> graphicsTest()
{
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext ctx;
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        return {0, 0, QString()};
    }
    QOpenGLExtraFunctions *f = ctx.extraFunctions();
    const QString renderer = QString::fromLatin1(reinterpret_cast<const char *>(f->glGetString(GL_RENDERER)));
    const int W = 1920, H = 1080;
    GLuint fbo, tex;
    f->glGenFramebuffers(1, &fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glGenTextures(1, &tex);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    f->glViewport(0, 0, W, H);
    auto shader = [f](GLenum type, const char *src) {
        GLuint s = f->glCreateShader(type);
        f->glShaderSource(s, 1, &src, nullptr);
        f->glCompileShader(s);
        return s;
    };
    const bool es = ctx.isOpenGLES();
    const char *vs = es ? "#version 300 es\nlayout(location=0) in vec3 p; uniform float k; out float l; void main(){ l = 0.5 + 0.5*sin(p.x*k+p.y); gl_Position = vec4(p,1.0); }"
                        : "#version 330 core\nlayout(location=0) in vec3 p; uniform float k; out float l; void main(){ l = 0.5 + 0.5*sin(p.x*k+p.y); gl_Position = vec4(p,1.0); }";
    const char *fs = es ? "#version 300 es\nprecision mediump float; in float l; out vec4 c; void main(){ c = vec4(0.2, l, 0.6, 0.35); }"
                        : "#version 330 core\nin float l; out vec4 c; void main(){ c = vec4(0.2, l, 0.6, 0.35); }";
    GLuint prog = f->glCreateProgram();
    f->glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    f->glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    f->glLinkProgram(prog);
    GLint linked = 0;
    f->glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        return {0, 0, renderer};
    }
    f->glUseProgram(prog);
    GLuint vao, vbo;
    f->glGenVertexArrays(1, &vao);
    f->glBindVertexArray(vao);
    f->glGenBuffers(1, &vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    // 1. fill rate: blended full-screen layers (the composited desktop of Aero)
    const float quad[] = {-1, -1, 0, 1, -1, 0, -1, 1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    f->glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    f->glDrawArrays(GL_TRIANGLES, 0, 6);
    f->glFinish();
    QElapsedTimer t;
    t.start();
    qint64 layers = 0;
    while (t.elapsed() < 1500) {
        for (int i = 0; i < 50; ++i) {
            f->glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        f->glFinish();
        layers += 50;
    }
    const double fill = layers * double(W) * H / 1e9 / (t.elapsed() / 1000.0);
    // 2. geometry: a dense mesh of small triangles (3D games)
    f->glDisable(GL_BLEND);
    const int grid = 700; // 2 * 700 * 700 = 980 000 triangles per draw
    std::vector<float> mesh;
    mesh.reserve(size_t(grid) * grid * 18);
    for (int y = 0; y < grid; ++y) {
        for (int x = 0; x < grid; ++x) {
            const float x0 = -1 + 2.0f * x / grid, x1 = -1 + 2.0f * (x + 1) / grid, y0 = -1 + 2.0f * y / grid, y1 = -1 + 2.0f * (y + 1) / grid;
            const float tri[] = {x0, y0, 0, x1, y0, 0, x0, y1, 0, x1, y0, 0, x1, y1, 0, x0, y1, 0};
            mesh.insert(mesh.end(), tri, tri + 18);
        }
    }
    f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.size() * sizeof(float)), mesh.data(), GL_STATIC_DRAW);
    const GLint k = f->glGetUniformLocation(prog, "k");
    f->glDrawArrays(GL_TRIANGLES, 0, GLsizei(mesh.size() / 3));
    f->glFinish();
    t.restart();
    qint64 tris = 0;
    int frame = 0;
    while (t.elapsed() < 1500) {
        f->glUniform1f(k, float(++frame));
        f->glDrawArrays(GL_TRIANGLES, 0, GLsizei(mesh.size() / 3));
        f->glFinish();
        tris += qint64(mesh.size() / 9);
    }
    const double mtris = tris / 1e6 / (t.elapsed() / 1000.0);
    f->glDeleteBuffers(1, &vbo);
    f->glDeleteVertexArrays(1, &vao);
    f->glDeleteProgram(prog);
    f->glDeleteFramebuffers(1, &fbo);
    f->glDeleteTextures(1, &tex);
    ctx.doneCurrent();
    return {fill, mtris, renderer};
}

inline bool onBattery()
{
    bool battery = false, mains = false;
    for (const QString &ps : QDir(QStringLiteral("/sys/class/power_supply")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/class/power_supply/") + ps;
        const QString type = readFile(base + QStringLiteral("/type")).trimmed();
        if (type == QLatin1String("Mains")) {
            mains = mains || readFile(base + QStringLiteral("/online")).trimmed() == QLatin1String("1");
        } else if (type == QLatin1String("Battery")) {
            battery = true;
        }
    }
    return battery && !mains;
}

/** "Rating this computer..." window, runs every test and saves the scores */
inline bool rate(QWidget *parent)
{
    if (onBattery()) {
        QMessageBox::information(parent, tr7("Performance Information and Tools"),
                                 tr7("The Windows Experience Index cannot be assessed while running on batteries. Connect the computer to a power outlet."));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Windows Experience Index"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(20, 18, 20, 18);
    auto *head = new QLabel(tr7("Rating this computer..."));
    QFont hf = head->font();
    hf.setPixelSize(15);
    head->setFont(hf);
    head->setStyleSheet(QStringLiteral("color: #1e3287;"));
    v->addWidget(head);
    v->addWidget(new QLabel(tr7("This might take a few minutes. During the assessment, the screen might flicker.")));
    auto *step = new QLabel;
    v->addWidget(step);
    auto *bar = new QProgressBar;
    bar->setRange(0, 5);
    v->addWidget(bar);
    dlg.setFixedWidth(460);
    dlg.show();
    auto status = [&](const QString &s, int n) {
        step->setText(s);
        bar->setValue(n);
        QCoreApplication::processEvents();
    };
    auto runAsync = [](auto fn) {
        using R = decltype(fn());
        std::atomic<bool> done{false};
        R result{};
        std::thread th([&] {
            result = fn();
            done = true;
        });
        while (!done) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        th.join();
        return result;
    };
    // WinSAT repeats every test and keeps the best pass, so a busy moment (another program, a background
    // update) does not lower the score: the same computer gets the same index every time
    const int passes = 3;
    status(tr7("Assessing processor..."), 0);
    double cpu = 0, mem = 0, fill = 0, tris = 0;
    QPair<double, double> disk{0, 0};
    QString renderer;
    for (int i = 0; i < passes; ++i) {
        cpu = qMax(cpu, runAsync(cpuTest));
    }
    status(tr7("Assessing memory..."), 1);
    for (int i = 0; i < passes; ++i) {
        mem = qMax(mem, runAsync(memoryTest));
    }
    status(tr7("Assessing primary hard disk..."), 2);
    for (int i = 0; i < passes; ++i) {
        const auto d = runAsync(diskTest);
        disk = {qMax(disk.first, d.first), qMax(disk.second, d.second)};
    }
    status(tr7("Assessing graphics..."), 3);
    for (int i = 0; i < passes; ++i) {
        const auto [f, t, r] = graphicsTest();
        fill = qMax(fill, f);
        tris = qMax(tris, t);
        renderer = r;
        QCoreApplication::processEvents();
    }
    status(tr7("Calculating the Windows Experience Index..."), 5);
    const double sCpu = round1(scale(cpu, {{10, 2.0}, {40, 4.0}, {120, 5.9}, {300, 7.0}, {800, 7.9}}));
    const double sMem = round1(scale(mem, {{1, 2.0}, {3, 4.0}, {6, 5.9}, {10, 7.0}, {20, 7.9}}));
    const double sGfx = round1(scale(fill, {{0.1, 2.0}, {0.5, 4.0}, {2, 5.9}, {8, 7.0}, {30, 7.9}}));
    const double sGame = round1(scale(tris, {{5, 2.0}, {30, 4.0}, {150, 5.9}, {600, 7.0}, {3000, 7.9}}));
    const double sSeq = scale(disk.first, {{20, 2.0}, {60, 4.0}, {120, 5.9}, {300, 7.0}, {700, 7.9}});
    const double sRand = scale(disk.second, {{50, 2.0}, {200, 4.0}, {1000, 5.9}, {8000, 7.0}, {30000, 7.9}});
    const double sDisk = round1((sSeq + sRand) / 2);
    QSettings &s = store();
    s.setValue(QStringLiteral("Scores/Processor"), sCpu);
    s.setValue(QStringLiteral("Scores/Memory"), sMem);
    s.setValue(QStringLiteral("Scores/Graphics"), sGfx);
    s.setValue(QStringLiteral("Scores/Gaming"), sGame);
    s.setValue(QStringLiteral("Scores/Disk"), sDisk);
    s.setValue(QStringLiteral("Raw/CpuMBs"), cpu);
    s.setValue(QStringLiteral("Raw/MemGBs"), mem);
    s.setValue(QStringLiteral("Raw/DiskSeqMBs"), disk.first);
    s.setValue(QStringLiteral("Raw/DiskIops"), disk.second);
    s.setValue(QStringLiteral("Raw/FillGpix"), fill);
    s.setValue(QStringLiteral("Raw/MTris"), tris);
    s.setValue(QStringLiteral("Raw/Renderer"), renderer);
    s.setValue(QStringLiteral("LastUpdate"), QDateTime::currentDateTime());
    s.sync();
    dlg.close();
    return true;
}

/* ---------------- pages ---------------- */

inline QString fmt(double v)
{
    return QLocale{}.toString(v, 'f', 1);
}

/** the original WinSATAPI badge (bitmap 211/212 big, 221/222 small) with the score on top */
inline QPixmap badgePixmap(double score, bool big)
{
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/seven-control/wei/");
    QPixmap pm(dir + (big ? QStringLiteral("badge-big") : QStringLiteral("badge-small")) + (score > 0 ? QString() : QStringLiteral("-grey")) + QStringLiteral(".png"));
    if (pm.isNull()) {
        pm = QPixmap(big ? QSize(80, 80) : QSize(32, 24));
        pm.fill(QColor(0x1c, 0x7c, 0xd5));
    }
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = QApplication::font();
    f.setBold(true);
    f.setPixelSize(big ? 40 : 14);
    if (big) {
        f.setLetterSpacing(QFont::AbsoluteSpacing, -1);
    }
    QPainterPath path;
    const QString text = score > 0 ? fmt(score) : QStringLiteral("—");
    const QFontMetricsF fm(f);
    const QRectF r(QPointF(0, 0), QSizeF(pm.size()) / pm.devicePixelRatio());
    const qreal x = r.center().x() - fm.horizontalAdvance(text) / 2.0;
    const qreal y = r.center().y() + (fm.ascent() - fm.descent()) / 2.0 - (big ? 1 : 0);
    path.addText(QPointF(x, y), f, text);
    // dark blue halo like the Win7 badge text
    p.setPen(QPen(QColor(0x0a, 0x3a, 0x6e, big ? 170 : 150), big ? 3 : 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawPath(path);
    return pm;
}

/** the big base score badge */
class ScoreTile : public QLabel
{
public:
    explicit ScoreTile(double score, QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setPixmap(badgePixmap(score, true));
        setFixedSize(80, 80);
    }
};

/** the score table: header on white under a grey line, light rows, darker base score column */
class ScoreTable : public QWidget
{
public:
    explicit ScoreTable(QWidget *parent = nullptr)
        : QWidget(parent)
        , grid(new QGridLayout(this))
    {
    }
    QGridLayout *grid;
    QWidget *header = nullptr, *baseHeader = nullptr, *lowest = nullptr;

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!header || !baseHeader) {
            return;
        }
        QPainter p(this);
        const int top = header->geometry().bottom() + grid->verticalSpacing() / 2 + 2;
        const int bottom = height() - 1;
        const int colX = baseHeader->geometry().left() - 4;
        p.fillRect(QRect(0, top, width(), bottom - top), QColor(0xec, 0xed, 0xf0));
        p.fillRect(QRect(colX, top, width() - colX, bottom - top), QColor(0xe2, 0xe4, 0xee));
        p.setPen(QColor(0xd4, 0xd4, 0xd4));
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor(0xdf, 0xdf, 0xe3));
        p.drawLine(0, bottom, width(), bottom);
        if (lowest) {
            const QRect c = lowest->geometry();
            p.fillRect(QRect(c.right() - 34, c.top() - 4, 40, c.height() + 8), QColor(0xd9, 0xdd, 0xe7));
        }
    }
};

/** small text window used by the "?" help links */
inline void helpText(QWidget *parent, const QString &title, const QString &text)
{
    QDialog d(parent);
    d.setWindowTitle(title);
    auto *v = new QVBoxLayout(&d);
    auto *h = Win7Programs::pageTitle(title);
    v->addWidget(h);
    auto *t = new QLabel(text);
    t->setWordWrap(true);
    t->setTextFormat(Qt::RichText);
    v->addWidget(t);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    bb->button(QDialogButtonBox::Close)->setText(tr7("Close"));
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.resize(520, d.sizeHint().height());
    d.exec();
}

inline QList<Win7Display::SideItem> sideLinks(const Nav &nav)
{
    return {{tr7("Adjust visual effects"), [] { Win7VisualFx::open(); }, QString(), true},
            {tr7("Adjust indexing options"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--indexing")}); }},
            {tr7("Adjust power settings"), [nav] { nav.open(Win7Power::PlansView); }},
            {tr7("Disk Cleanup"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--cleanmgr")}); }},
            {tr7("Advanced Tools"), [nav] { nav.open(ToolsView); }}};
}

inline QList<Win7Display::SideItem> seeAlso(const Nav &nav)
{
    return {{tr7("Action Center"), [nav] { nav.open(Win7Action::CenterView); }}};
}

inline QLabel *bold(const QString &t)
{
    auto *l = new QLabel(t);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

inline QWidget *buildPerfPage(const Nav &nav)
{
    auto *content = new QWidget;
    auto *root = Win7Display::pageWithSide(Win7Display::sidePane(nav.home, sideLinks(nav), seeAlso(nav)), content);
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(32, 18, 30, 18);
    v->setSpacing(8);
    v->addWidget(Win7Programs::pageTitle(tr7("Rate and improve your computer's performance")));
    QSettings &s = store();
    const bool rated = s.contains(QStringLiteral("LastUpdate"));
    if (!rated) {
        v->addWidget(Win7Programs::wrapped(tr7("Your computer has not been rated yet.")));
        auto *rateBtn = new QPushButton(tr7("Rate this computer"));
        v->addWidget(rateBtn, 0, Qt::AlignLeft);
        QObject::connect(rateBtn, &QPushButton::clicked, rateBtn, [nav, rateBtn] {
            if (rate(rateBtn->window())) {
                nav.open(PerfView);
            }
        });
        v->addStretch(1);
        return root;
    }
    v->addWidget(Win7Programs::wrapped(tr7("The Windows Experience Index assesses key system components on a scale of 1.0 to 7.9.")));
    v->addSpacing(8);
    // the table, as PerfCenterCPL draws it
    auto *table = new ScoreTable;
    auto *g = table->grid;
    g->setContentsMargins(2, 8, 0, 6);
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(12);
    table->header = bold(tr7("Component"));
    g->addWidget(table->header, 0, 0);
    g->addWidget(new QLabel(tr7("What is rated")), 0, 1);
    g->addWidget(new QLabel(tr7("Subscore")), 0, 2, Qt::AlignRight);
    table->baseHeader = new QLabel(tr7("Base score"));
    table->baseHeader->setMinimumWidth(170);
    qobject_cast<QLabel *>(table->baseHeader)->setAlignment(Qt::AlignCenter);
    g->addWidget(table->baseHeader, 0, 3);
    struct Row {
        QString name, what, key;
    };
    const QList<Row> rows{{tr7("Processor:"), tr7("Calculations per second"), QStringLiteral("Processor")},
                          {tr7("Memory (RAM):"), tr7("Memory operations per second"), QStringLiteral("Memory")},
                          {tr7("Graphics:"), tr7("Desktop performance for Windows Aero"), QStringLiteral("Graphics")},
                          {tr7("Gaming graphics:"), tr7("3D business and gaming graphics performance"), QStringLiteral("Gaming")},
                          {tr7("Primary hard disk:"), tr7("Disk data transfer rate"), QStringLiteral("Disk")}};
    double base = 7.9;
    QString lowest;
    for (const Row &r : rows) {
        const double sc = s.value(QStringLiteral("Scores/") + r.key).toDouble();
        if (sc < base) {
            base = sc;
            lowest = r.key;
        }
    }
    int row = 1;
    for (const Row &r : rows) {
        auto *name = bold(r.name);
        name->setContentsMargins(2, 0, 0, 0);
        g->addWidget(name, row, 0);
        auto *what = Win7Programs::wrapped(r.what);
        what->setMinimumWidth(200);
        what->setMaximumWidth(260);
        g->addWidget(what, row, 1);
        auto *sc = new QLabel(fmt(s.value(QStringLiteral("Scores/") + r.key).toDouble()));
        sc->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sc->setMinimumWidth(30);
        sc->setContentsMargins(0, 0, 3, 0);
        g->addWidget(sc, row, 2);
        if (r.key == lowest) {
            table->lowest = sc;
        }
        ++row;
    }
    auto *tileBox = new QVBoxLayout;
    tileBox->setSpacing(4);
    tileBox->addStretch(1);
    tileBox->addWidget(new ScoreTile(base), 0, Qt::AlignHCenter);
    auto *det = new QLabel(tr7("Determined by\nlowest subscore"));
    det->setAlignment(Qt::AlignHCenter);
    tileBox->addWidget(det);
    tileBox->addStretch(1);
    g->addLayout(tileBox, 1, 3, 5, 1);
    g->setColumnStretch(1, 1);
    table->setMaximumWidth(830);
    v->addWidget(table);
    // links under the table
    auto *links = new QGridLayout;
    links->setContentsMargins(20, 6, 0, 6);
    links->setHorizontalSpacing(14);
    links->setVerticalSpacing(10);
    auto icon = [](const QString &name, int size) {
        auto *l = new QLabel;
        l->setPixmap(QIcon::fromTheme(name).pixmap(size, size));
        return l;
    };
    links->addWidget(icon(QStringLiteral("seven-wei-help"), 32), 0, 0);
    links->addWidget(new Link(tr7("What do these numbers mean?"), [content] {
        helpText(content->window(), tr7("What do these numbers mean?"),
                 tr7("<p>The Windows Experience Index measures the capability of your computer's hardware and software configuration and expresses this measurement as a number called a <b>base score</b>. A higher base score generally means that your computer will perform better and faster than a computer with a lower base score, especially when performing more advanced and resource-intensive tasks.</p><p>Each hardware component receives an individual <b>subscore</b>. Your computer's base score is determined by the lowest subscore, because overall performance is limited by the slowest component.</p><p>Currently, scores range from 1.0 to 7.9.</p><ul><li><b>1.0–2.9</b> - enough for general office work and browsing the Internet;</li><li><b>3.0–4.9</b> - can run Aero and many programs at the same time;</li><li><b>5.0–7.9</b> - suited for games, video editing and other demanding tasks.</li></ul>"));
    }), 0, 1);
    links->addWidget(icon(QStringLiteral("seven-wei-help"), 32), 1, 0);
    links->addWidget(new Link(tr7("Tips for improving your computer's\nperformance."), [content] {
        helpText(content->window(), tr7("Tips for improving your computer's performance"),
                 tr7("<ul><li>Close programs you are not using and remove unneeded programs from startup.</li><li>Free up disk space with Disk Cleanup.</li><li>Turn off unneeded visual effects in Adjust visual effects.</li><li>Choose the High performance power plan when the computer is plugged in.</li><li>Check Device Manager for driver updates.</li><li>To raise the base score, replace the component with the lowest subscore.</li></ul>"));
    }), 1, 1);
    auto *printRow = new QHBoxLayout;
    printRow->setSpacing(10);
    printRow->addWidget(icon(QStringLiteral("seven-wei-print"), 48), 0, Qt::AlignTop);
    printRow->addWidget(new Link(tr7("View and print detailed performance\nand system information"), [nav] {
        nav.open(DetailsView);
    }), 0, Qt::AlignTop);
    links->addLayout(printRow, 0, 2, 2, 1, Qt::AlignTop | Qt::AlignRight);
    links->setColumnStretch(1, 1);
    auto *lw = new QWidget;
    lw->setLayout(links);
    lw->setMaximumWidth(830);
    v->addWidget(lw);
    // online box
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("weionline"));
    box->setStyleSheet(QStringLiteral("#weionline { border: 1px solid #d5dfe5; background: white; }"));
    auto *bh = new QHBoxLayout(box);
    bh->setContentsMargins(10, 14, 10, 14);
    bh->setSpacing(18);
    bh->addWidget(icon(QStringLiteral("seven-wei-online"), 48));
    bh->addWidget(new Link(tr7("Learn more about the scores and\nsoftware online"), [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Windows_System_Assessment_Tool")));
    }));
    bh->addStretch(1);
    box->setMaximumWidth(830);
    v->addWidget(box);
    v->addSpacing(4);
    auto *bottom = new QHBoxLayout;
    auto *when = new QVBoxLayout;
    when->addWidget(new QLabel(tr7("Your scores are current")));
    when->addWidget(new QLabel(tr7("Last refreshed: %1").arg(s.value(QStringLiteral("LastUpdate")).toDateTime().toString(QStringLiteral("dd.MM.yyyy H:mm:ss")))));
    bottom->addLayout(when);
    bottom->addStretch(1);
    auto *shield = new QLabel;
    shield->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
    bottom->addWidget(shield);
    bottom->addWidget(new Link(tr7("Re-run the assessment"), [nav, content] {
        if (rate(content->window())) {
            nav.open(PerfView);
        }
    }));
    auto *bw = new QWidget;
    bw->setLayout(bottom);
    bw->setMaximumWidth(830);
    v->addWidget(bw);
    v->addStretch(1);
    return root;
}

inline QWidget *buildToolsPage(const Nav &nav)
{
    auto *content = new QWidget;
    auto *root = Win7Display::pageWithSide(Win7Display::sidePane(nav.home, {}, seeAlso(nav)), content);
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(32, 18, 30, 18);
    v->setSpacing(10);
    v->addWidget(Win7Programs::pageTitle(tr7("Advanced Tools")));
    v->addWidget(Win7Programs::wrapped(tr7("These tools give you more information about performance.")));
    v->addSpacing(6);
    struct Tool {
        QString icon, title, desc;
        std::function<void()> open;
        bool shield = false;
    };
    const QList<Tool> tools{
        {QStringLiteral("seven-perf"), tr7("Clear all Windows Experience Index scores and re-rate the system"),
         tr7("Force all Windows Experience Index tests to run again."),
         [nav] {
             store().clear();
             nav.open(PerfView);
         },
         true},
        {QStringLiteral("seven-ev-app"), tr7("View performance details in Event log"), tr7("View information about issues that affect performance."),
         [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--eventvwr")}); }},
        {QStringLiteral("seven-resmon"), tr7("Open Resource Monitor"), tr7("View real-time usage of system resources and manage services and programs."),
         [] { launch(QDir::homePath() + QStringLiteral("/.local/bin/seven-taskmgr"), {QStringLiteral("--resmon")}); }},
        {QStringLiteral("utilities-system-monitor"), tr7("Open Task Manager"), tr7("Get information about the programs and processes running on your computer."),
         [] { launchWin7Tool(QStringLiteral("seven-taskmgr"), QStringLiteral("plasma-systemmonitor")); }},
        {QStringLiteral("seven-cps-system"), tr7("View advanced system details in System Information"), tr7("View details about the hardware and software components of your computer."),
         [] { launchWin7Tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter")); }},
        {QStringLiteral("preferences-desktop-effects"), tr7("Adjust the appearance and performance of Windows"),
         tr7("Choose visual effect settings."), [] { Win7VisualFx::open(); }, true},
    };
    for (const Tool &t : tools) {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(t.icon).pixmap(32, 32));
        row->addWidget(ic, 0, Qt::AlignTop);
        auto *col = new QVBoxLayout;
        col->setSpacing(2);
        auto *title = new QHBoxLayout;
        if (t.shield) {
            auto *sh = new QLabel;
            sh->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
            title->addWidget(sh);
        }
        title->addWidget(new Link(t.title, t.open));
        title->addStretch(1);
        col->addLayout(title);
        col->addWidget(Win7Programs::wrapped(t.desc));
        row->addLayout(col, 1);
        auto *w = new QWidget;
        w->setLayout(row);
        w->setMaximumWidth(720);
        v->addWidget(w);
    }
    v->addStretch(1);
    return root;
}

inline QString gpuMemory()
{
    const QString nv = readFile(QStringLiteral("/proc/driver/nvidia/version"));
    QProcess p;
    p.start(QStringLiteral("nvidia-smi"), {QStringLiteral("--query-gpu=memory.total"), QStringLiteral("--format=csv,noheader,nounits")});
    if (p.waitForFinished(3000) && p.exitCode() == 0) {
        const int mb = QString::fromUtf8(p.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), 0, 0).toInt();
        if (mb > 0) {
            return tr7("%1 MB").arg(QLocale{}.toString(mb));
        }
    }
    Q_UNUSED(nv)
    for (const QString &c : QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card?")}, QDir::Dirs | QDir::System)) {
        const qint64 vram = readFile(QStringLiteral("/sys/class/drm/%1/device/mem_info_vram_total").arg(c)).trimmed().toLongLong();
        if (vram > 0) {
            return tr7("%1 MB").arg(QLocale{}.toString(vram / 1048576));
        }
    }
    return tr7("Not detected");
}

inline QWidget *buildDetailsPage(const Nav &nav)
{
    auto *content = new QWidget;
    auto *root = Win7Display::pageWithSide(Win7Display::sidePane(nav.home, {}, seeAlso(nav)), content);
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(32, 18, 30, 18);
    v->setSpacing(6);
    v->addWidget(Win7Programs::pageTitle(tr7("Performance Information and System Details")));
    QSettings &s = store();
    auto *g = new QGridLayout;
    g->setHorizontalSpacing(30);
    g->setVerticalSpacing(5);
    int row = 0;
    auto section = [&](const QString &t) {
        auto *l = bold(t);
        l->setStyleSheet(QStringLiteral("QLabel { color: #1e3287; padding-top: 10px; }"));
        g->addWidget(l, row++, 0, 1, 2);
    };
    auto line = [&](const QString &k, const QString &val) {
        g->addWidget(new QLabel(k), row, 0, Qt::AlignTop);
        auto *l = Win7Programs::wrapped(val.isEmpty() ? tr7("Not detected") : val);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        g->addWidget(l, row++, 1);
    };
    const QLocale uk;
    auto num = [&uk](double d, int prec) {
        return uk.toString(d, 'f', prec);
    };
    section(tr7("Component - Details"));
    line(tr7("Processor"), tr7("%1 (%2 MB/s data compression)").arg(fmt(s.value(QStringLiteral("Scores/Processor")).toDouble()),
                                                                                          num(s.value(QStringLiteral("Raw/CpuMBs")).toDouble(), 0)));
    line(tr7("Memory (RAM)"), tr7("%1 (%2 GB/s)").arg(fmt(s.value(QStringLiteral("Scores/Memory")).toDouble()), num(s.value(QStringLiteral("Raw/MemGBs")).toDouble(), 1)));
    line(tr7("Graphics"), tr7("%1 (%2 Gpixels/s)").arg(fmt(s.value(QStringLiteral("Scores/Graphics")).toDouble()), num(s.value(QStringLiteral("Raw/FillGpix")).toDouble(), 1)));
    line(tr7("Gaming graphics"), tr7("%1 (%2 million triangles/s)").arg(fmt(s.value(QStringLiteral("Scores/Gaming")).toDouble()), num(s.value(QStringLiteral("Raw/MTris")).toDouble(), 0)));
    line(tr7("Primary hard disk"), tr7("%1 (%2 MB/s, %3 4K operations/s)").arg(fmt(s.value(QStringLiteral("Scores/Disk")).toDouble()),
                                                                                                       num(s.value(QStringLiteral("Raw/DiskSeqMBs")).toDouble(), 0),
                                                                                                       num(s.value(QStringLiteral("Raw/DiskIops")).toDouble(), 0)));
    section(tr7("System"));
    const QString dmi = QStringLiteral("/sys/class/dmi/id/");
    line(tr7("Manufacturer"), readFile(dmi + QStringLiteral("sys_vendor")).trimmed());
    line(tr7("Model", "system"), readFile(dmi + QStringLiteral("product_version")).trimmed());
    line(tr7("Total system memory"),
         tr7("%1 GB RAM").arg(num(keyValue(readFile(QStringLiteral("/proc/meminfo")), QStringLiteral("MemTotal"), QLatin1Char(':')).section(QLatin1Char(' '), 0, 0).toDouble() / 1048576.0, 2)));
    line(tr7("System type"), QSysInfo::currentCpuArchitecture().contains(QLatin1String("64")) ? tr7("64-bit Operating System") : tr7("32-bit Operating System"));
    line(tr7("Number of processor cores"), QString::number(std::thread::hardware_concurrency()));
    line(tr7("64-bit capable"), tr7("Yes"));
    section(tr7("Storage", "system"));
    const QStorageInfo sys(QStringLiteral("/"));
    line(tr7("Disk partition (/)"), tr7("%1 GB free (%2 GB total)").arg(num(sys.bytesAvailable() / 1e9, 0), num(sys.bytesTotal() / 1e9, 0)));
    section(tr7("Graphics"));
    line(tr7("Display adapter type"), s.value(QStringLiteral("Raw/Renderer")).toString());
    line(tr7("Total available graphics memory"), gpuMemory());
    const QScreen *scr = QGuiApplication::primaryScreen();
    line(tr7("Primary monitor resolution"), scr ? QStringLiteral("%1x%2").arg(scr->size().width() * scr->devicePixelRatio()).arg(scr->size().height() * scr->devicePixelRatio()) : QString());
    g->setColumnMinimumWidth(0, 300);
    g->setColumnStretch(2, 1);
    v->addLayout(g);
    v->addStretch(1);
    return root;
}

inline QWidget *build(int view, const Nav &nav)
{
    if (view == ToolsView) {
        return buildToolsPage(nav);
    }
    if (view == DetailsView) {
        return buildDetailsPage(nav);
    }
    return buildPerfPage(nav);
}
}
