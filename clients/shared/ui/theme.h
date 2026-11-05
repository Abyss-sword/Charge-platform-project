#pragma once
#include <QFont>
class QApplication;
namespace Ui {
struct Theme final
{
    static constexpr const char *Brand = "#102F46";
    static constexpr const char *Action = "#3478C5";
    static constexpr const char *Charging = "#24AD7A";
    static constexpr const char *Energy = "#32B9C8";
    static constexpr const char *Positive = "#82C75B";
    static constexpr const char *Warning = "#EDA14B";
    static constexpr const char *Error = "#DF6B65";
    static constexpr const char *Prediction = "#756FC5";
    static constexpr const char *Background = "#F3F7F9";
    static constexpr const char *Card = "#FFFFFF";
    static constexpr const char *Text = "#183044";
    static constexpr const char *Secondary = "#6B7B88";
    static constexpr const char *Border = "#DCE6EC";
    static constexpr const char *HeroEnd = "#1C5773";
    static constexpr int Space = 8;
    static constexpr int Margin = 16;
    static constexpr int Radius = 8;
    static constexpr int CardRadius = 12;
    static constexpr int HeroRadius = 16;
    static constexpr int BodyPixels = 14;
    static constexpr int CaptionPixels = 12;
    static constexpr int TitlePixels = 22;
    static constexpr int SectionPixels = 17;
    static constexpr int TouchHeight = 44;
    static constexpr int MotionFastMs = 150;
    static constexpr int MotionNormalMs = 300;
    static constexpr int MotionSlowMs = 500;
    static QFont font();
    static void apply(QApplication &application);
};
}
