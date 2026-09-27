#include "ui/LicenceDialog.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QDialogButtonBox>
#include <QString>
#include <QStringLiteral>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <Qt>

using VLMS::T;

namespace {

using VLMS::Locale;

// Not string-table keys. They read the same in all three languages, so a key
// would be three identical entries -- and identical entries would defeat
// test_core_LicenceStrings.TheArabicIsNotTheEnglish.
constexpr auto kLinkedIn = "https://www.linkedin.com/in/mlbh/";
constexpr auto kEmail = "mohamed.ben-hassine@mailfence.com";

QString paragraph(const QString& text)
{
    return QStringLiteral("<p>") + text.toHtmlEscaped() + QStringLiteral("</p>");
}

QString clause(const QString& head, const QString& body)
{
    return QStringLiteral("<h3>") + head.toHtmlEscaped() + QStringLiteral("</h3>")
           + paragraph(body);
}

QString licenceHtml()
{
    const QString direction = Locale::isRtl() ? QStringLiteral("rtl") : QStringLiteral("ltr");

    QString html = QStringLiteral("<div dir=\"%1\">").arg(direction);
    html += QStringLiteral("<h2>") + T("licence.title").toHtmlEscaped() + QStringLiteral("</h2>");
    html += paragraph(T("licence.intro"));
    html += clause(T("licence.clause.ownership.head"), T("licence.clause.ownership.body"));
    html += clause(T("licence.clause.data.head"), T("licence.clause.data.body"));
    html += clause(T("licence.clause.warranty.head"), T("licence.clause.warranty.body"));
    html += clause(T("licence.clause.liability.head"), T("licence.clause.liability.body"));
    html += clause(T("licence.clause.support.head"), T("licence.clause.support.body"));
    html += QStringLiteral("<h3>") + T("licence.contact.head").toHtmlEscaped()
            + QStringLiteral("</h3>");
    html += QStringLiteral("<p>") + T("licence.contact.author").toHtmlEscaped()
            + QStringLiteral("<br>") + QString::fromLatin1(kLinkedIn)
            + QStringLiteral("<br>") + QString::fromLatin1(kEmail) + QStringLiteral("</p>");
    html += QStringLiteral("</div>");
    return html;
}

}  // namespace

LicenceDialog::LicenceDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
}

void LicenceDialog::buildUi()
{
    setWindowTitle(T("licence.title"));
    resize(560, 620);

    auto* layout = new QVBoxLayout(this);

    // A QTextBrowser rather than a QLabel in a QScrollArea: the text scrolls,
    // and a librarian can select and copy a clause out of it.
    m_body = new QTextBrowser(this);
    m_body->setObjectName(QStringLiteral("licenceBody"));
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setOpenExternalLinks(false);
    m_body->setLayoutDirection(Locale::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    m_body->setHtml(licenceHtml());
    layout->addWidget(m_body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString LicenceDialog::documentText() const
{
    return m_body->toPlainText();
}
