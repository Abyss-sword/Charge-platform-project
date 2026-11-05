#include <QtTest>
#include "adapters/mock_data_provider.h"
#include "admin_mock_data_provider.h"
class MockDataProviderTest final : public QObject {
    Q_OBJECT
private slots:
    void userNormalComesFromJson() {
        MockDataProvider p;
        for (auto d : {UserMockDataset::Login, UserMockDataset::Stations, UserMockDataset::Order}) { const auto r=p.load(d); QVERIFY(r.valid); QVERIFY(!r.payload.isEmpty()); }
        QCOMPARE(p.load(UserMockDataset::Stations).payload.value("items").toArray().size(), 2);
        QCOMPARE(p.load(UserMockDataset::Order).payload.value("items").toArray().first().toObject().value("id").toString(), QString("MOCK-ORDER-01"));
    }
    void adminNormalComesFromJson() {
        AdminMockDataProvider p;
        for (auto d : {AdminMockDataset::Login, AdminMockDataset::Overview, AdminMockDataset::Devices, AdminMockDataset::Orders}) { const auto r=p.load(d); QVERIFY(r.valid); QVERIFY(!r.payload.isEmpty()); }
        QVERIFY(p.load(AdminMockDataset::Overview).payload.contains("metrics"));
        QCOMPARE(p.load(AdminMockDataset::Devices).payload.value("items").toArray().size(), 2);
    }
    void emptyIsValidArray() {
        MockDataProvider u; AdminMockDataProvider a;
        for (auto d : {UserMockDataset::Stations, UserMockDataset::Order}) { const auto r=u.load(d, MockScenario::Empty); QVERIFY(r.valid); QCOMPARE(r.state, Ui::PageState::Empty); QVERIFY(r.payload.value("items").isArray()); QVERIFY(r.payload.value("items").toArray().isEmpty()); }
        for (auto d : {AdminMockDataset::Devices, AdminMockDataset::Orders}) { const auto r=a.load(d, AdminMockScenario::Empty); QVERIFY(r.valid); QCOMPARE(r.state, Ui::PageState::Empty); QVERIFY(r.payload.value("items").toArray().isEmpty()); }
    }
    void failuresAreDeterministic() {
        MockDataProvider u; AdminMockDataProvider a;
        const auto ut=u.load(UserMockDataset::Stations, MockScenario::Timeout); QCOMPARE(ut.errorCode, QString("SIMULATED_TIMEOUT")); QCOMPARE(ut.state, Ui::PageState::Error);
        const auto us=u.load(UserMockDataset::Stations, MockScenario::ServiceError); QCOMPARE(us.errorCode, QString("SIMULATED_SERVICE_ERROR")); QVERIFY(!us.message.isEmpty());
        QCOMPARE(u.load(UserMockDataset::Stations, MockScenario::Offline).state, Ui::PageState::Offline);
        QCOMPARE(a.load(AdminMockDataset::Overview, AdminMockScenario::Timeout).errorCode, QString("SIMULATED_TIMEOUT"));
        QCOMPARE(a.load(AdminMockDataset::Overview, AdminMockScenario::ServiceError).errorCode, QString("SIMULATED_SERVICE_ERROR"));
        QCOMPARE(a.load(AdminMockDataset::Overview, AdminMockScenario::Offline).state, Ui::PageState::Offline);
    }
    void malformedJsonFailsWithoutCrash() {
        MockDataProvider u; AdminMockDataProvider a;
        const auto ur=u.parse("{malformed"); QVERIFY(!ur.valid); QCOMPARE(ur.errorCode, QString("SIMULATED_MALFORMED_JSON"));
        const auto ar=a.parse("[]"); QVERIFY(!ar.valid); QCOMPARE(ar.errorCode, QString("SIMULATED_MALFORMED_JSON"));
    }
    void scenariosRecoverNormal() {
        MockDataProvider u; AdminMockDataProvider a;
        QVERIFY(!u.load(UserMockDataset::Order, MockScenario::Offline).valid); QVERIFY(u.load(UserMockDataset::Order).valid);
        QVERIFY(!a.load(AdminMockDataset::Orders, AdminMockScenario::ServiceError).valid); QVERIFY(a.load(AdminMockDataset::Orders).valid);
    }
};
QTEST_MAIN(MockDataProviderTest)
#include "mock_data_provider_test.moc"
