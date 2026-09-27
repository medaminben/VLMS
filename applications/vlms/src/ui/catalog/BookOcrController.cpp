#include "ui/catalog/BookOcrController.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Ocr/Ocr.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QProgressDialog>
#include <QTimer>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

using VLMS::Strings;

constexpr int kOcrPollIntervalMs = 80;

}  // namespace

BookOcrController::BookOcrController(QWidget* dialogParent, QObject* parent)
    : QObject(parent),
      m_dialogParent(dialogParent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kOcrPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &BookOcrController::poll);
}

BookOcrController::~BookOcrController()
{
    m_job.reset();
    VLMS::Ocr::releaseCachedEngine();
}

void BookOcrController::start(const QString& languages)
{
    if (m_job != nullptr) {
        return;
    }

    if (!VLMS::Ocr::isAvailable() || languages.isEmpty()) {
        VLMS::showWarning(
            m_dialogParent,
            T("ocr.title"),
            T("ocr.unavailable"));
        return;
    }

    const QString path = VLMS::askForImageFile(
        m_dialogParent,
        T("ocr.selectImage"),
        T("ocr.imageFilter"));
    if (path.isEmpty()) {
        return;
    }

    VLMS::Ocr::Request request;
    request.imagePath = path.toStdString();
    request.languages = languages.toStdString();
    m_job = VLMS::Ocr::Job::start(std::move(request));

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

void BookOcrController::poll()
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

    const VLMS::Ocr::Result result = m_job->take();
    finish();

    using VLMS::Ocr::Status;
    switch (result.status) {
        case Status::Ok:
            emit textReady(QString::fromStdString(result.text));
            return;
        case Status::Cancelled:
            return;
        case Status::Unavailable:
            VLMS::showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.unavailable"));
            return;
        case Status::ImageMissing:
            VLMS::showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.imageMissing"));
            return;
        case Status::StartFailed:
            VLMS::showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.startFailed"));
            return;
        case Status::Empty:
            VLMS::showWarning(
                m_dialogParent,
                T("ocr.title"),
                T("ocr.empty"));
            return;
        case Status::Failed:
            break;
    }
    VLMS::showWarning(
        m_dialogParent,
        T("ocr.title"),
        T("ocr.failed"));
}

void BookOcrController::finish()
{
    m_pollTimer->stop();
    m_job.reset();
    if (m_progress != nullptr) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
}
