#include "ui/services/OcrService.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Ocr/Ocr.h>
#include "ui/views/UiHelpers.h"

#include <QProgressDialog>
#include <QTimer>

namespace {

constexpr int kOcrPollIntervalMs = 80;

}  // namespace

namespace VLMS {

OcrService::OcrService(QObject* parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kOcrPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &OcrService::poll);
}

OcrService::~OcrService()
{
    m_job.reset();
    Ocr::releaseCachedEngine();
}

void OcrService::start(QWidget* dialogParent, const QString& imagePath, const QString& languages)
{
    if (m_job != nullptr) {
        return;
    }

    m_dialogParent = dialogParent;

    if (!Ocr::isAvailable() || languages.isEmpty()) {
        showWarning(
            m_dialogParent,
            T("ocr.title"),
            T("ocr.unavailable"));
        return;
    }

    if (imagePath.isEmpty()) {
        showWarning(
            m_dialogParent,
            T("ocr.title"),
            T("ocr.imageMissing"));
        return;
    }

    Ocr::Request request;
    request.imagePath = imagePath.toStdString();
    request.languages = languages.toStdString();
    m_job = Ocr::Job::start(std::move(request));

    m_progress = new QProgressDialog(
        T("ocr.working"),
        T("common.cancel"),
        0,
        0,
        m_dialogParent);
    m_progress->setWindowTitle(T("ocr.title"));
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setMinimumDuration(400);
    m_progress->setValue(0);
    connect(m_progress, &QProgressDialog::canceled, this, [this]() {
        if (m_job != nullptr) {
            m_job->cancel();
        }
        if (m_progress != nullptr) {
            m_progress->setLabelText(T("ocr.cancelling"));
        }
    });

    m_pollTimer->start();
}

void OcrService::poll()
{
    if (m_job == nullptr) {
        m_pollTimer->stop();
        return;
    }

    if (!m_job->done()) {
        const int percent = m_job->progressPercent();
        if (m_progress != nullptr && percent > 0) {
            if (m_progress->maximum() == 0) {
                m_progress->setMaximum(100);
            }
            m_progress->setValue(percent);
        }
        return;
    }

    const Ocr::Result result = m_job->take();
    finish();

    using Ocr::Status;
    switch (result.status) {
        case Status::Ok:
            emit textReady(QString::fromStdString(result.text));
            return;
        case Status::Cancelled:
            return;
        case Status::Unavailable:
            showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.unavailable"));
            return;
        case Status::ImageMissing:
            showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.imageMissing"));
            return;
        case Status::StartFailed:
            showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.startFailed"));
            return;
        case Status::Empty:
            showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.empty"));
            return;
        case Status::Failed:
            break;
    }
    showWarning(
        m_dialogParent,
        T("ocr.title"),
        T("ocr.failed"));
}

void OcrService::finish()
{
    m_pollTimer->stop();
    m_job.reset();
    if (m_progress != nullptr) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
}

}  // namespace VLMS
