#pragma once

#include "core/diagnosis.h"
#include "core/rules.h"
#include "reader/profile-reader.h"

#include <QWidget>

#include <optional>
#include <string>

class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace shindan {

// 緑・黄・赤のランプ。画像を使わず円を描く
class LampWidget : public QWidget {
	Q_OBJECT
public:
	explicit LampWidget(QWidget *parent = nullptr);
	void setLamp(Lamp lamp);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	Lamp lamp_ = Lamp::Green;
};

class ShindanDock : public QWidget {
	Q_OBJECT
public:
	explicit ShindanDock(QWidget *parent = nullptr);
	~ShindanDock() override;

	// trigger はログに残す契機の名前
	void rediagnose(const char *trigger);
	// プロファイルの切替えと読み込みの完了では、配信先の選択を OBS の設定から選び直す
	void resetPlatformSelection();

protected:
	void showEvent(QShowEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void loadRules();
	void render();
	QString itemValueText(Item item) const;
	QString itemGuideText(const PlatformRules &rules, const ItemResult &result) const;
	QString commentText(const ItemResult &result) const;

	std::optional<RuleSet> rules_;
	std::string rulesError_;
	Snapshot snapshot_;
	bool userChosePlatform_ = false;

	LampWidget *lamp_ = nullptr;
	QLabel *summary_ = nullptr;
	QComboBox *platform_ = nullptr;
	QPushButton *rediagnoseButton_ = nullptr;
	QWidget *itemsHost_ = nullptr;
	QVBoxLayout *itemsLayout_ = nullptr;
	QLabel *footer_ = nullptr;
};

} // namespace shindan
