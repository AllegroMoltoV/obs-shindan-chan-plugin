#include "shindan-dock.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace shindan {

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString T(const std::string &key)
{
	return T(key.c_str());
}

QString fill(QString text, const ItemResult &r)
{
	text.replace("%MIN%", QString::number(r.expectedMin));
	text.replace("%REC%", QString::number(r.expectedRecommended));
	text.replace("%MAX%", QString::number(r.expectedMax));
	return text;
}

QString fpsText(double fps)
{
	if (std::fabs(fps - std::round(fps)) < 0.001)
		return QString::number(static_cast<int>(std::round(fps)));
	return QString::number(fps, 'f', 2);
}

QString codecLabel(const std::string &codec)
{
	if (codec == "h264")
		return "H.264";
	if (codec == "hevc")
		return "HEVC";
	if (codec == "av1")
		return "AV1";
	return T("Value.UnknownCodec");
}

QString kbps(int value)
{
	return T("Value.Kbps").replace("%1", QString::number(value));
}

QString changed(const QString &configured, const QString &effective)
{
	return T("Value.ChangedByService").replace("%1", configured).replace("%2", effective);
}

struct StatusStyle {
	const char *mark;
	const char *color;
};

StatusStyle styleFor(Status s)
{
	switch (s) {
	case Status::Ok:
		return {"●", "#2EAD6B"};
	case Status::Warn:
		return {"▲", "#E08A00"};
	case Status::Manual:
		return {"？", "#3D7BD9"};
	case Status::Auto:
	case Status::Info:
	case Status::Pending:
		break;
	}
	return {"－", "#8A8A8A"};
}

QString joinInts(const std::vector<int> &values)
{
	QStringList parts;
	for (int v : values)
		parts << QString::number(v);
	return parts.join(T("Value.ListSeparator"));
}

} // namespace

LampWidget::LampWidget(QWidget *parent) : QWidget(parent)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void LampWidget::setLamp(Lamp lamp)
{
	lamp_ = lamp;
	update();
}

QSize LampWidget::sizeHint() const
{
	return QSize(36, 36);
}

void LampWidget::paintEvent(QPaintEvent *)
{
	QColor color;
	switch (lamp_) {
	case Lamp::Green:
		color = QColor("#2EAD6B");
		break;
	case Lamp::Yellow:
		color = QColor("#F2B705");
		break;
	case Lamp::Red:
		color = QColor("#E5484D");
		break;
	}
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	int d = std::min(width(), height()) - 4;
	QRectF r((width() - d) / 2.0, (height() - d) / 2.0, d, d);
	p.setPen(QPen(color.darker(140), 2));
	p.setBrush(color);
	p.drawEllipse(r);
}

ShindanDock::ShindanDock(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	auto *header = new QHBoxLayout();
	lamp_ = new LampWidget(this);
	summary_ = new QLabel(this);
	summary_->setWordWrap(true);
	header->addWidget(lamp_);
	header->addWidget(summary_, 1);
	root->addLayout(header);

	auto *controls = new QHBoxLayout();
	controls->addWidget(new QLabel(T("Platform"), this));
	platform_ = new QComboBox(this);
	controls->addWidget(platform_, 1);
	rediagnoseButton_ = new QPushButton(T("Rediagnose"), this);
	controls->addWidget(rediagnoseButton_);
	root->addLayout(controls);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	itemsHost_ = new QWidget(scroll);
	itemsLayout_ = new QVBoxLayout(itemsHost_);
	itemsLayout_->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(itemsHost_);
	root->addWidget(scroll, 1);

	footer_ = new QLabel(this);
	footer_->setWordWrap(true);
	footer_->setTextFormat(Qt::RichText);
	footer_->setOpenExternalLinks(true);
	root->addWidget(footer_);

	loadRules();
	if (rules_) {
		for (const auto &p : rules_->platforms)
			platform_->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
	}

	connect(platform_, &QComboBox::activated, this, [this](int) {
		userChosePlatform_ = true;
		render();
	});
	connect(rediagnoseButton_, &QPushButton::clicked, this, [this]() { rediagnose("button"); });

	// 設定の画面を閉じたことを知らせるイベントが OBS に無いため、ダイアログの Hide を拾う
	qApp->installEventFilter(this);
}

ShindanDock::~ShindanDock()
{
	qApp->removeEventFilter(this);
}

void ShindanDock::loadRules()
{
	char *path = obs_module_file("rules.ini");
	if (!path) {
		rulesError_ = "rules.ini not found";
		obs_log(LOG_ERROR, "%s", rulesError_.c_str());
		return;
	}
	char *text = os_quick_read_utf8_file(path);
	bfree(path);
	if (!text) {
		rulesError_ = "cannot read rules.ini";
		obs_log(LOG_ERROR, "%s", rulesError_.c_str());
		return;
	}
	rules_ = parseRules(text, rulesError_);
	bfree(text);
	if (!rules_)
		obs_log(LOG_ERROR, "%s", rulesError_.c_str());
}

void ShindanDock::resetPlatformSelection()
{
	userChosePlatform_ = false;
}

void ShindanDock::rediagnose(const char *trigger)
{
	snapshot_ = readSnapshot();
	logSnapshot(snapshot_, trigger);

	if (!userChosePlatform_ && rules_) {
		// U3: OBS の配信先から選ぶ。判別できないときは YouTube Live
		std::string id = snapshot_.detectedPlatform.empty() ? "youtube" : snapshot_.detectedPlatform;
		int index = platform_->findData(QString::fromStdString(id));
		if (index >= 0)
			platform_->setCurrentIndex(index);
	}
	render();
}

void ShindanDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	rediagnose("dock_shown");
}

bool ShindanDock::eventFilter(QObject *watched, QEvent *event)
{
	const QEvent::Type type = event->type();
	if ((type == QEvent::Show || type == QEvent::Hide) && watched->isWidgetType() &&
	    watched->objectName() == QStringLiteral("OBSBasicSettings")) {
		if (type == QEvent::Show) {
			hookApplyButton(static_cast<QWidget *>(watched));
		} else {
			// 保存の処理が終わってから読むため、イベントループに戻ってから診断する
			QTimer::singleShot(0, this, [this]() { rediagnose("settings_closed"); });
		}
	}
	return QWidget::eventFilter(watched, event);
}

void ShindanDock::hookApplyButton(QWidget *settingsDialog)
{
	// 「適用」を押したときも、画面を開いたまま診断をやり直す。
	// OBS は buttonBox の clicked で設定を保存する (OBSBasicSettings::on_buttonBox_clicked)。
	// buttonBox の名前は OBS の内部の作りのため、見つからないときは閉じたときの再診断だけになる
	auto *buttons = settingsDialog->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
	if (!buttons) {
		obs_log(LOG_WARNING, "settings buttonBox not found; rediagnose on Apply is disabled");
		return;
	}
	const char *hookedProperty = "obsShindanChanHooked";
	if (buttons->property(hookedProperty).toBool())
		return;
	buttons->setProperty(hookedProperty, true);
	// OBS の保存の処理より後に接続されるため、保存が終わってから呼ばれる。念のためイベントループにも戻す
	connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton *button) {
		if (buttons->buttonRole(button) == QDialogButtonBox::ApplyRole)
			QTimer::singleShot(0, this, [this]() { rediagnose("settings_applied"); });
	});
}

QString ShindanDock::itemValueText(Item item) const
{
	const Snapshot &s = snapshot_;
	switch (item) {
	case Item::Resolution:
		return QString("%1×%2").arg(s.width).arg(s.height);
	case Item::Fps:
		return fpsText(s.fps);
	case Item::VideoBitrate: {
		QString value = s.configVideoBitrate == s.effectiveVideoBitrate
					? kbps(s.effectiveVideoBitrate)
					: changed(kbps(s.configVideoBitrate), kbps(s.effectiveVideoBitrate));
		return value + T("Value.Separator") + codecLabel(s.codec);
	}
	case Item::RateControl: {
		QString configured = QString::fromStdString(s.configRateControl);
		QString effective = QString::fromStdString(s.effectiveRateControl);
		return configured == effective ? effective : changed(configured, effective);
	}
	case Item::Keyint: {
		auto sec = [](int v) {
			return v <= 0 ? T("Value.Auto") : T("Value.Seconds").replace("%1", QString::number(v));
		};
		return s.configKeyint == s.effectiveKeyint ? sec(s.effectiveKeyint)
							   : changed(sec(s.configKeyint), sec(s.effectiveKeyint));
	}
	case Item::Profile: {
		auto prof = [](const std::string &v) {
			return v.empty() ? T("Value.EncoderDefault") : QString::fromStdString(v);
		};
		return s.configProfile == s.effectiveProfile ? prof(s.effectiveProfile)
							     : changed(prof(s.configProfile), prof(s.effectiveProfile));
	}
	case Item::AudioSampleRate: {
		QString value = T("Value.Hz").replace("%1", QString::number(s.audioSampleRate));
		if (s.runtimeSampleRate > 0 && s.runtimeSampleRate != s.audioSampleRate)
			value += T("Value.Separator") + T("Value.RestartPending");
		return value + T("Value.Separator") + QString::fromStdString(s.channelSetup);
	}
	case Item::AudioBitrate:
		return s.configAudioBitrate == s.effectiveAudioBitrate
			       ? kbps(s.effectiveAudioBitrate)
			       : changed(kbps(s.configAudioBitrate), kbps(s.effectiveAudioBitrate));
	}
	return {};
}

QString ShindanDock::itemGuideText(const PlatformRules &rules, const ItemResult &r) const
{
	switch (r.item) {
	case Item::Resolution:
		return rules.maxHeight > 0 ? fill(T("Guide.Resolution"), r) : T("Guide.None");
	case Item::Fps:
		if (!rules.fpsAllowed.empty()) {
			QStringList parts;
			for (double f : rules.fpsAllowed)
				parts << fpsText(f);
			return T("Guide.FpsList").replace("%1", parts.join(T("Value.ListSeparator")));
		}
		return rules.maxFps > 0 ? T("Guide.FpsMax").replace("%1", fpsText(rules.maxFps)) : T("Guide.None");
	case Item::VideoBitrate: {
		if (r.expectedRecommended <= 0)
			return T("Guide.None");
		QString key = r.expectedMin > 0 ? "Guide.VideoBitrate.MinRec" : "Guide.VideoBitrate.Rec";
		QString text = fill(T(key.toStdString()), r);
		if (r.expectedMax > 0)
			text += T("Value.Separator") + fill(T("Guide.VideoBitrate.Max"), r);
		return text;
	}
	case Item::RateControl:
		return "CBR";
	case Item::Keyint:
		if (rules.keyintMin == rules.keyintMax)
			return T("Value.Seconds").replace("%1", QString::number(rules.keyintRecommended));
		return T("Guide.KeyintRange")
			.replace("%1", QString::number(rules.keyintMin))
			.replace("%2", QString::number(rules.keyintMax))
			.replace("%3", QString::number(rules.keyintRecommended));
	case Item::Profile: {
		if (rules.profiles.empty())
			return T("Guide.None");
		QStringList parts;
		for (const auto &p : rules.profiles)
			parts << QString::fromStdString(p);
		return parts.join(T("Value.ListSeparator"));
	}
	case Item::AudioSampleRate:
		if (rules.audioSampleRates.empty())
			return T("Guide.None");
		return T("Value.Hz").replace("%1", joinInts(rules.audioSampleRates));
	case Item::AudioBitrate: {
		QStringList parts;
		if (r.expectedMin > 0)
			parts << fill(T("Guide.AudioBitrate.Min"), r);
		if (r.expectedRecommended > 0)
			parts << fill(T("Guide.AudioBitrate.Rec"), r);
		if (r.expectedMax > 0)
			parts << fill(T("Guide.AudioBitrate.Max"), r);
		return parts.isEmpty() ? T("Guide.None") : parts.join(T("Value.Separator"));
	}
	}
	return {};
}

QString ShindanDock::commentText(const ItemResult &r) const
{
	std::string key = std::string("Comment.") + itemKey(r.item) + "." + r.reason;
	return fill(T(key), r);
}

void ShindanDock::render()
{
	while (QLayoutItem *child = itemsLayout_->takeAt(0)) {
		delete child->widget();
		delete child;
	}

	if (!rules_) {
		lamp_->setLamp(Lamp::Red);
		summary_->setText(T("Error.Rules") + "\n" + QString::fromStdString(rulesError_));
		footer_->clear();
		return;
	}

	std::string id = platform_->currentData().toString().toStdString();
	const PlatformRules *rules = rules_->find(id);
	if (!rules)
		rules = &rules_->platforms.front();

	DiagnosisResult result = diagnose(*rules, toDiagnosisInput(snapshot_));
	lamp_->setLamp(result.lamp);
	const char *summaryKey = result.lamp == Lamp::Green    ? "Summary.Green"
				 : result.lamp == Lamp::Yellow ? "Summary.Yellow"
							       : "Summary.Red";
	QString summary = T(summaryKey).replace("%1", QString::number(result.warnCount));
	if (snapshot_.multitrack && rules->multitrackAuto)
		summary += "\n" + T("Summary.Multitrack");
	summary_->setText(summary);

	for (const auto &item : result.items) {
		StatusStyle st = styleFor(item.status);
		QString html =
			QString("<span style=\"color:%1\">%2</span> <b>%3</b>: %4<br>"
				"<span style=\"color:#8A8A8A\">%5: %6</span><br>%7")
				.arg(st.color, st.mark, T(std::string("Item.") + itemKey(item.item)).toHtmlEscaped(),
				     itemValueText(item.item).toHtmlEscaped(), T("Guide.Label").toHtmlEscaped(),
				     itemGuideText(*rules, item).toHtmlEscaped(), commentText(item).toHtmlEscaped());
		auto *label = new QLabel(html, itemsHost_);
		label->setTextFormat(Qt::RichText);
		label->setWordWrap(true);
		itemsLayout_->addWidget(label);
	}
	itemsLayout_->addStretch(1);

	QStringList links;
	for (const auto &p : rules_->platforms) {
		links << T("Footer.Source")
				 .replace("%1", QString("<a href=\"%1\">%2</a>")
							.arg(QString::fromStdString(p.url).toHtmlEscaped(),
							     QString::fromStdString(p.name).toHtmlEscaped()))
				 .replace("%2", QString::fromStdString(p.checked).toHtmlEscaped());
	}
	footer_->setText(links.join("<br>"));
}

} // namespace shindan
