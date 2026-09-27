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

/// Owns the OCR worker for the book editor. The dialog stays a composer.
class BookOcrController final : public QObject {
    Q_OBJECT

public:
    explicit BookOcrController(QWidget* dialogParent, QObject* parent = nullptr);
    ~BookOcrController() override;

    void start(const QString& languages);

signals:
    void textReady(const QString& text);

private:
    void poll();
    void finish();

    QWidget* m_dialogParent = nullptr;
    std::unique_ptr<VLMS::Ocr::Job> m_job;
    QProgressDialog* m_progress = nullptr;
    QTimer* m_pollTimer = nullptr;
};
