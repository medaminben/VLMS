#include "ui/LanguageSelector.h"

#include <VLMS/Core/Strings.h>
#include "ui/FlagIcons.h"
#include "ui/UiHelpers.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSize>
#include <QStringList>

namespace VLMS {
namespace {

// Layout order. The header inherits the application direction, so Arabic
// mirrors the row without a second list.
const QStringList& languageCodes()
{
    static const QStringList codes{QStringLiteral("ar"), QStringLiteral("fr"),
                                   QStringLiteral("en")};
    return codes;
}

}  // namespace

LanguageSelector::LanguageSelector(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("languageSelector"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    for (const QString& code : languageCodes()) {
        auto* button = new QPushButton(this);
        button->setObjectName(QStringLiteral("languageButton"));
        button->setProperty("langCode", code);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setFixedSize(kButtonSize, kButtonSize);
        m_group->addButton(button);
        layout->addWidget(button);
    }

    m_current = languageCodes().constFirst();

    connect(m_group, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* button) {
        const QString code = button->property("langCode").toString();
        // An exclusive group keeps the pressed button pressed, so a second
        // click on the language already in use is not a change.
        if (code == m_current) {
            return;
        }
        m_current = code;
        updateButtons();
        emit languageSelected(code);
    });

    retranslateUi();
}

QString LanguageSelector::currentLanguage() const
{
    return m_current;
}

void LanguageSelector::setCurrentLanguage(const QString& code)
{
    if (!languageCodes().contains(code)) {
        return;
    }
    m_current = code;
    updateButtons();
}

void LanguageSelector::retranslateUi()
{
    for (QAbstractButton* button : m_group->buttons()) {
        const QString code = button->property("langCode").toString();
        const QString name = T(ss(QStringLiteral("lang.%1").arg(code)));
        // The flags carry no text, so the name of the language lives here and
        // in the accessible name, which is all a screen reader is given.
        button->setToolTip(name);
        button->setAccessibleName(name);
    }
    updateButtons();
}

void LanguageSelector::updateButtons()
{
    for (QAbstractButton* button : m_group->buttons()) {
        const QString code = button->property("langCode").toString();
        const bool active = (code == m_current);
        const int iconSize = active ? kActiveIconSize : kIdleIconSize;

        // An exclusive QButtonGroup silently ignores setChecked(false) on the
        // button that is currently checked -- it will not uncheck itself
        // without another button taking its place. That is harmless here: the
        // loop reaches the newly active button later in the same pass and its
        // setChecked(true) re-establishes the invariant regardless of order.
        button->setChecked(active);
        button->setProperty("active", active);
        button->setIcon(languageFlagIcon(code, iconSize, devicePixelRatioF(), !active));
        button->setIconSize(QSize(iconSize, iconSize));
        refreshWidgetStyle(button);
    }
}

}  // namespace VLMS
