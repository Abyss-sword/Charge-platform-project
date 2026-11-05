#pragma once
#include <QMetaType>
enum class PrimaryPage { Home, Stations, Orders, History, Account };
constexpr int PrimaryPageCount = 5;
enum class SecondaryPage { StationDetail, Navigation, ReservationConfirm, ChargingStatus,
                           ActiveOrderRecovery, Settlement, Analysis };
constexpr int SecondaryPageCount = 7;
Q_DECLARE_METATYPE(PrimaryPage)
