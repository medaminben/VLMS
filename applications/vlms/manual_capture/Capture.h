#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

class MainWindow;
class QAbstractButton;
class QTableWidget;

namespace ManualCapture {

class Capture {
public:
    Capture(MainWindow& window, QString outRoot, QString lang);

    [[nodiscard]] MainWindow& window() const;
    [[nodiscard]] const QString& lang() const;

    /// Clicks the header nav button whose text is T(navKey), e.g. "nav.members",
    /// then processes events until the page settles.
    void goTo(const char* navKey);
    /// The visible page's widget (the QStackedWidget's current widget).
    [[nodiscard]] QWidget* page() const;

    /// First visible QAbstractButton under root whose text is T(key) (after
    /// stripping '&'). Throws std::runtime_error naming the key when absent.
    [[nodiscard]] QAbstractButton* button(const char* key, QWidget* root = nullptr) const;
    /// First QTableWidget under root (default: page()).
    [[nodiscard]] QTableWidget* table(QWidget* root = nullptr) const;
    /// First child of type W under root whose objectName is name.
    template <typename W>
    [[nodiscard]] W* named(const QString& name, QWidget* root = nullptr) const;

    void click(QWidget* widget);           // QTest::mouseClick at its centre + settle()
    void typeInto(QWidget* edit, const QString& text);  // clear, QTest::keyClicks, settle()
    void settle(int ms = 150);             // processEvents + qWait so paging/queries finish

    /// Opens a modal by running trigger (which calls exec()), then runs inside on the
    /// modal widget while it is up. inside must close the modal (reject/accept/answer).
    /// Nested modals: call openModal again from inside.
    void openModal(const std::function<void()>& trigger,
                   const std::function<void(QWidget* modal)>& inside);

    /// Grab of widget (default: the whole window) with any visible popup
    /// (QComboBox view, QMenu) composited at its on-screen position.
    [[nodiscard]] QPixmap grab(QWidget* widget = nullptr) const;
    /// Grab of the window cropped to the union of widgets' window rects, padded by 12px.
    [[nodiscard]] QPixmap grabRegion(const QList<QWidget*>& widgets) const;

    /// Numbered circles ①..⑳ at each target's top inline-start corner (right corner in
    /// RTL), offset 6px inward; targets are in the coordinate space of `base`, which is
    /// the widget that was grabbed. Returns the annotated pixmap.
    [[nodiscard]] QPixmap callouts(QPixmap image, QWidget* base,
                                   const QList<QWidget*>& targets) const;
    /// Same, with rects already in image coordinates (for table cells, header sections).
    [[nodiscard]] QPixmap calloutsAt(QPixmap image, const QList<QRect>& rects) const;

    /// Writes <out>/shots/<lang>/<id>.png; logs "shot <lang>/<id> WxH".
    void save(const QString& id, const QPixmap& image) const;

    /// Restores the neutral state described on ShotFn.
    void reset();

private:
    MainWindow& m_window;
    QString m_outRoot;
    QString m_lang;
};

template <typename W>
W* Capture::named(const QString& name, QWidget* root) const
{
    QWidget* base = root != nullptr ? root : page();
    if (base == nullptr) {
        return nullptr;
    }
    return base->findChild<W*>(name);
}

}  // namespace ManualCapture
