#include "ui/views/BulkAction.h"

#include "ui/views/UiHelpers.h"

#include <QProgressDialog>

namespace VLMS {
namespace {

class QtBulkProgress final : public BulkProgress {
public:
    QtBulkProgress(QWidget* parent, const QString& title, int total) : m_dialog(parent)
    {
        m_dialog.setWindowTitle(title);
        m_dialog.setMinimumDuration(500);
        m_dialog.setWindowModality(Qt::WindowModal);
        m_dialog.setRange(0, total);
        m_dialog.setValue(0);
    }

    void step(int done, const QString& label) override
    {
        if (!label.isEmpty()) {
            m_dialog.setLabelText(label);
        }
        m_dialog.setValue(done);
    }

    [[nodiscard]] bool cancelled() const override { return m_dialog.wasCanceled(); }

private:
    QProgressDialog m_dialog;
};

}  // namespace

bool QtBulkUi::confirm(const QString& title, const QString& text)
{
    return askYesNo(m_parent, title, text);
}

void QtBulkUi::inform(const QString& title, const QString& text)
{
    showInformation(m_parent, title, text);
}

std::unique_ptr<BulkProgress> QtBulkUi::startProgress(const QString& title, int total)
{
    return std::make_unique<QtBulkProgress>(m_parent, title, total);
}

}  // namespace VLMS

int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Core::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Core::Status(const BulkRow&)>& act)
{
    VLMS::QtBulkUi ui(parent);
    return VLMS::runBulkFlow(ui, texts, rows, check, act).done;
}
