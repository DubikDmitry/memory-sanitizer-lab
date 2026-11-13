#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTextEdit>
#include <QTextCursor>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QStringList>
#include <QPair>

namespace {

struct ParsedReport {
    enum class Kind { Ok, Asan, Lsan, Ubsan, Unknown };
    Kind kind = Kind::Unknown;
    QString errorType, operation;
    QString locationFile, locationLine, locationFunc;
    QString allocFile, allocLine;
    QString freedFile, freedLine;
    QString leakBytes, leakAllocs;
    QString ubsanMessage;
    int exitCode = 0;
};

QString shortPath(const QString& p) {
    int idx = p.indexOf("/src/");
    if (idx >= 0) return p.mid(idx + 1);        // "src/main.cpp"
    idx = p.lastIndexOf('/');
    if (idx >= 0) return p.mid(idx + 1);        // "main.cpp"
    return p;
}

QPair<QString, QString> firstMainCppAfter(const QString& text, int from) {
    if (from < 0) return {};
    QRegularExpression re(R"((\S*main\.cpp):(\d+))");
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        if (m.capturedStart(0) >= from)
            return {shortPath(m.captured(1)), m.captured(2)};
    }
    return {};
}

ParsedReport parseReport(const QString& text, int exitCode) {
    ParsedReport p;
    p.exitCode = exitCode;

    // UBSan
    {
        QRegularExpression re(R"(([^\n:]+):(\d+):(\d+):\s*runtime error:\s*([^\n]+))");
        auto m = re.match(text);
        if (m.hasMatch()) {
            p.kind = ParsedReport::Kind::Ubsan;
            p.locationFile = shortPath(m.captured(1));
            p.locationLine = m.captured(2);
            p.ubsanMessage = m.captured(4).trimmed();
            return p;
        }
    }
    // LSan
    {
        QRegularExpression re(R"(SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked in (\d+) allocation)");
        auto m = re.match(text);
        if (m.hasMatch()) {
            p.kind = ParsedReport::Kind::Lsan;
            p.leakBytes = m.captured(1);
            p.leakAllocs = m.captured(2);
            auto loc = firstMainCppAfter(text, text.indexOf("allocated from:"));
            p.allocFile = loc.first; p.allocLine = loc.second;
            return p;
        }
    }
    // ASan
    {
        QRegularExpression re(R"(SUMMARY: AddressSanitizer: (\S+) (\S+):(\d+) in (\S+))");
        auto m = re.match(text);
        if (m.hasMatch()) {
            p.kind = ParsedReport::Kind::Asan;
            p.errorType = m.captured(1);
            p.locationFile = shortPath(m.captured(2));
            p.locationLine = m.captured(3);
            p.locationFunc = m.captured(4);

            QRegularExpression opRe(R"((READ|WRITE) of size (\d+))");
            auto om = opRe.match(text);
            if (om.hasMatch()) p.operation = om.captured(1) + " of size " + om.captured(2);

            int allocFrom = text.indexOf("previously allocated by");
            if (allocFrom < 0) allocFrom = text.indexOf("allocated by thread");
            auto allocLoc = firstMainCppAfter(text, allocFrom);
            p.allocFile = allocLoc.first; p.allocLine = allocLoc.second;

            auto freedLoc = firstMainCppAfter(text, text.indexOf("freed by"));
            p.freedFile = freedLoc.first; p.freedLine = freedLoc.second;
            return p;
        }
    }
    if (exitCode == 0) p.kind = ParsedReport::Kind::Ok;
    return p;
}

QString bannerHtml(const ParsedReport& p) {
    const QString style = "padding:10px;border-radius:4px;font-weight:bold;font-size:13px;";
    switch (p.kind) {
    case ParsedReport::Kind::Ok:
        return QString("<div style='background:#1e7d32;color:white;%1'>&#10004; OK &mdash; сценарий завершился без ошибок (exit=0)</div>").arg(style);
    case ParsedReport::Kind::Asan: {
        QString loc = p.locationLine.isEmpty() ? "" : QString(" &middot; %1:%2").arg(p.locationFile, p.locationLine);
        QString op  = p.operation.isEmpty()  ? "" : QString(" &middot; %1").arg(p.operation);
        return QString("<div style='background:#b3261e;color:white;%1'>&#10008; AddressSanitizer: %2%3%4</div>")
            .arg(style, p.errorType.toHtmlEscaped(), op, loc);
    }
    case ParsedReport::Kind::Lsan:
        return QString("<div style='background:#b26a00;color:white;%1'>&#9888; LeakSanitizer: утечка %2 байт в %3 аллокациях</div>")
            .arg(style,
                 p.leakBytes.isEmpty() ? "?" : p.leakBytes,
                 p.leakAllocs.isEmpty() ? "?" : p.leakAllocs);
    case ParsedReport::Kind::Ubsan:
        return QString("<div style='background:#b3261e;color:white;%1'>&#10008; UBSan: %2</div>")
            .arg(style, p.ubsanMessage.toHtmlEscaped());
    default:
        return QString("<div style='background:#555;color:white;%1'>Процесс завершился с кодом %2 (без распознанного отчёта)</div>")
            .arg(style).arg(p.exitCode);
    }
}

QString detailsHtml(const ParsedReport& p) {
    QStringList rows;
    auto row = [&](const QString& k, const QString& v) {
        if (!v.isEmpty())
            rows << QString("<tr><td style='padding:3px 10px 3px 0;color:#666;white-space:nowrap;'>%1</td>"
                            "<td style='padding:3px 0;'><code>%2</code></td></tr>")
                    .arg(k, v.toHtmlEscaped());
    };
    row("Тип ошибки", p.errorType);
    row("Операция", p.operation);
    row("Место ошибки", p.locationLine.isEmpty() ? "" :
        p.locationFile + ":" + p.locationLine +
        (p.locationFunc.isEmpty() ? "" : " (" + p.locationFunc + ")"));
    row("Место аллокации", p.allocLine.isEmpty() ? "" : p.allocFile + ":" + p.allocLine);
    row("Место освобождения", p.freedLine.isEmpty() ? "" : p.freedFile + ":" + p.freedLine);
    row("Утечка", p.leakBytes.isEmpty() ? "" :
        p.leakBytes + " байт / " + p.leakAllocs + " аллокаций");
    if (rows.isEmpty()) return {};
    return "<table style='margin-top:10px;font-size:12px;'>" + rows.join("") + "</table>";
}

} // namespace

class LabWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit LabWindow(QString binaryPath, QWidget* parent = nullptr)
        : QMainWindow(parent), m_binary(std::move(binaryPath))
    {
        auto* central = new QWidget(this);
        auto* root = new QHBoxLayout(central);

        auto* left = new QWidget;
        auto* leftLayout = new QVBoxLayout(left);
        leftLayout->addWidget(makeGroup("Дефекты", {"oob", "uaf", "overflow", "leak"}));
        leftLayout->addWidget(makeGroup("Исправления", {"oob-fixed", "uaf-fixed", "overflow-fixed", "leak-fixed"}));
        leftLayout->addStretch();
        root->addWidget(left, 1);

        auto* right = new QWidget;
        auto* rightLayout = new QVBoxLayout(right);

        m_banner = new QLabel("Готов. Выберите сценарий слева.");
        m_banner->setTextFormat(Qt::RichText);
        m_banner->setWordWrap(true);
        rightLayout->addWidget(m_banner);

        m_details = new QLabel;
        m_details->setTextFormat(Qt::RichText);
        m_details->setWordWrap(true);
        rightLayout->addWidget(m_details);

        m_raw = new QTextEdit;
        m_raw->setReadOnly(true);
        m_raw->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_raw->setLineWrapMode(QTextEdit::NoWrap);
        rightLayout->addWidget(m_raw, 1);

        root->addWidget(right, 3);

        setCentralWidget(central);
        resize(1100, 680);
        setWindowTitle("MemorySanitizer Lab — визуальный демонстратор");
    }

private:
    QWidget* makeGroup(const QString& title, const QStringList& scenarios) {
        auto* box = new QGroupBox(title);
        auto* lay = new QVBoxLayout(box);
        for (const QString& s : scenarios) {
            auto* btn = new QPushButton(s);
            btn->setMinimumHeight(32);
            connect(btn, &QPushButton::clicked, this, [this, s]{ runScenario(s); });
            lay->addWidget(btn);
        }
        return box;
    }

    void runScenario(const QString& scenario) {
        if (m_proc) return;

        m_banner->setText(QString("<div style='padding:10px;background:#eee;border-radius:4px;'>Запуск <code>%1</code>&hellip;</div>").arg(scenario));
        m_details->clear();
        m_raw->clear();

        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::SeparateChannels);

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (scenario.startsWith("overflow"))
            env.insert("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1");
        if (scenario.startsWith("leak"))
            env.insert("ASAN_OPTIONS", "detect_leaks=1");
        m_proc->setProcessEnvironment(env);

        m_proc->setProgram(m_binary);
        m_proc->setArguments({scenario});

        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]{
            m_raw->moveCursor(QTextCursor::End);
            m_raw->insertPlainText(QString::fromLocal8Bit(m_proc->readAllStandardOutput()));
        });
        connect(m_proc, &QProcess::readyReadStandardError, this, [this]{
            m_raw->moveCursor(QTextCursor::End);
            m_raw->insertPlainText(QString::fromLocal8Bit(m_proc->readAllStandardError()));
        });
        connect(m_proc, &QProcess::finished, this,
                [this](int exitCode, QProcess::ExitStatus){
            if (!m_proc) return;
            m_raw->moveCursor(QTextCursor::End);
            m_raw->insertPlainText(QString::fromLocal8Bit(m_proc->readAllStandardOutput()));
            m_raw->insertPlainText(QString::fromLocal8Bit(m_proc->readAllStandardError()));
            m_proc->deleteLater();
            m_proc = nullptr;

            const ParsedReport parsed = parseReport(m_raw->toPlainText(), exitCode);
            m_banner->setText(bannerHtml(parsed));
            m_details->setText(detailsHtml(parsed));
        });
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError){
            m_banner->setText("<div style='padding:10px;background:#b3261e;color:white;border-radius:4px;'>Не удалось запустить процесс: "
                              + m_proc->errorString().toHtmlEscaped() + "</div>");
            if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
        });

        m_proc->start();
    }

    QString m_binary;
    QProcess* m_proc = nullptr;
    QLabel* m_banner = nullptr;
    QLabel* m_details = nullptr;
    QTextEdit* m_raw = nullptr;
};

#include "gui_main.moc"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const QString binPath = QCoreApplication::applicationDirPath() + "/memory_lab";
    LabWindow w(binPath);
    w.show();
    return app.exec();
}