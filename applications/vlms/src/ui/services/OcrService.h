#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace VLMS::Ocr {
class Job;
}

class QProgressDialog;
class QTimer;
class QWidget;

namespace VLMS {

/// Owns the OCR worker for the book editor. The dialog stays a composer.
class OcrService final : public QObject {
    Q_OBJECT

public:
    explicit OcrService(QObject* parent = nullptr);
    ~OcrService() override;

    /// Starts OCR for the current cover image path held by the editor.
    void start(QWidget* dialogParent, const QString& imagePath, const QString& languages);

signals:
    void textReady(const QString& text);

private:
    void poll();
    void finish();

    QWidget* m_dialogParent = nullptr;
    std::unique_ptr<Ocr::Job> m_job;
    QProgressDialog* m_progress = nullptr;
    QTimer* m_pollTimer = nullptr;
};

}  // namespace VLMS
