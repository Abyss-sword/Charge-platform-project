#pragma once
#include "widgets/page_scaffold.h"
#include "adapters/mock_data_provider.h"
class QTimer;
class PreviewPage : public Ui::PageScaffold
{
    Q_OBJECT
public:
    explicit PreviewPage(const PagePreview &preview, QWidget *parent = nullptr);
    void appendCards(const QList<PreviewCard> &cards);
signals:
    void navigationRequested(PrimaryPage page);
    void secondaryRequested(SecondaryPage page);
protected:
    // S3 规格：可见页每 3 秒刷新、离开页面停止定时器。
    // enablePolling() 由“已接通正式 Provider”的页面在构造时调用；未调用的页面（mock/预览）不轮询。
    void enablePolling();
    void disablePolling();
    virtual void refresh() {}
    // 轮询期间的加载态入口：仅在尚未处于 Normal 时置 Loading，避免每 3 秒闪一次加载态。
    void markRefreshing();
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    QTimer *pollTimer_ = nullptr;
    bool pollingEnabled_ = false;
};
