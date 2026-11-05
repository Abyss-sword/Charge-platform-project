#pragma once

#include "adapters/mock_data_provider.h"
#include "adapters/user_login_provider.h"

// UI-only provider. It does not implement USER_LOGIN or any transport behavior.
class MockUserLoginProvider final : public IUserLoginProvider
{
    Q_OBJECT
public:
    explicit MockUserLoginProvider(QObject *parent = nullptr);

    void setScenario(MockScenario scenario) { scenario_ = scenario; }
    MockScenario scenario() const { return scenario_; }
    void submit(const LoginRequest &request) override;

private:
    MockDataProvider data_;
    MockScenario scenario_ = MockScenario::Normal;
};
