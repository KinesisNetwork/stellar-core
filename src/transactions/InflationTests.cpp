// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SHA.h"
#include "herder/LedgerCloseData.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "lib/catch.hpp"
#include "main/Application.h"
#include "main/Config.h"
#include "test/TestAccount.h"
#include "test/TestExceptions.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "transactions/InflationOpFrame.h"
#include "util/Logging.h"
#include "util/Timer.h"
#include <functional>

using namespace stellar;
using namespace stellar::txtest;

static const unsigned maxWinners = 2000u;

TEST_CASE("inflation", "[tx][inflation]")
{
    Config const& cfg = getTestConfig(0);

    VirtualClock::time_point inflationStart;
    // inflation starts on 1-jul-2014
    time_t start = getTestDate(1, 7, 2014);
    inflationStart = VirtualClock::from_time_t(start);

    VirtualClock clock;
    clock.setCurrentTime(inflationStart);

    auto app = createTestApplication(clock, cfg);

    auto root = TestAccount::createRoot(*app);

    app->start();

    SECTION("not time")
    {
        for_all_versions(*app, [&] {
            closeLedgerOn(*app, 2, 30, 6, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);

            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                0);

            closeLedgerOn(*app, 3, 1, 7, 2014);

            auto txFrame = root.tx({inflation()});

            closeLedgerOn(*app, 4, 7, 7, 2014, {txFrame});
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                1);

            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                1);

            closeLedgerOn(*app, 5, 8, 7, 2014);
            root.inflation();
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                2);

            closeLedgerOn(*app, 6, 14, 7, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                2);

            closeLedgerOn(*app, 7, 15, 7, 2014);
            root.inflation();
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                3);

            closeLedgerOn(*app, 8, 21, 7, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(
                app->getLedgerManager().getCurrentLedgerHeader().inflationSeq ==
                3);
        });
    }

    SECTION("total coins")
    {
        auto clh = app->getLedgerManager().getCurrentLedgerHeader();
        REQUIRE(clh.feePool == 0);
        REQUIRE(clh.totalCoins == 1000000000000000000);

        auto minBalance = app->getLedgerManager().getMinBalance(0);
        auto rootBalance = root.getBalance();

        auto voter1 = TestAccount{*app, getAccount("voter1"), 0};
        auto voter2 = TestAccount{*app, getAccount("voter2"), 0};

        Hash seed = sha256(app->getConfig().NETWORK_PASSPHRASE + "feepool");
        SecretKey feeKey = SecretKey::fromSeed(seed);
        AccountID targetKey = feeKey.getPublicKey();

        auto voter1tx = root.tx({createAccount(voter1, rootBalance / 6)});
        voter1tx->getEnvelope().tx.fee = 999999999;
        auto voter2tx = root.tx({createAccount(voter2, rootBalance / 3)});
        auto targettx = root.tx({createAccount(targetKey, minBalance)});

        closeLedgerOn(*app, 2, 21, 7, 2014, {voter1tx, voter2tx, targettx});

        AccountFrame::pointer inflationTarget;
        inflationTarget = loadAccount(targetKey, *app);

        clh = app->getLedgerManager().getCurrentLedgerHeader();
        REQUIRE(clh.feePool == (999999999 + 2 * 100));
        REQUIRE(clh.totalCoins == 1000000000000000000);

        auto beforeInflationRoot = root.getBalance();
        auto beforeInflationVoter1 = voter1.getBalance();
        auto beforeInflationVoter2 = voter2.getBalance();
        auto beforeInflationTarget = inflationTarget->getBalance();

        REQUIRE(beforeInflationRoot + beforeInflationVoter1 +
                    beforeInflationVoter2 + beforeInflationTarget +
                    clh.feePool ==
                clh.totalCoins);

        auto inflationTx = root.tx({inflation()});

        for_versions_to(7, *app, [&] {
            clh = app->getLedgerManager().getCurrentLedgerHeader();
            REQUIRE(clh.feePool == (999999999 + 2 * 100));
            closeLedgerOn(*app, 3, 21, 7, 2014, {inflationTx});

            clh = app->getLedgerManager().getCurrentLedgerHeader();
            REQUIRE(clh.feePool == 0);
            REQUIRE(clh.totalCoins == 1000000000000000000);

            auto afterInflationRoot = root.getBalance();
            auto afterInflationVoter1 = voter1.getBalance();
            auto afterInflationVoter2 = voter2.getBalance();
            inflationTarget = loadAccount(targetKey, *app);
            auto afterInflationTarget = inflationTarget->getBalance();

            REQUIRE(beforeInflationRoot == afterInflationRoot + 100);
            REQUIRE(beforeInflationVoter1 == afterInflationVoter1);
            REQUIRE(beforeInflationVoter2 == afterInflationVoter2);
            REQUIRE(beforeInflationTarget ==
                    afterInflationTarget - (999999999 + 3 * 100));

            REQUIRE(afterInflationRoot + afterInflationVoter1 +
                        afterInflationVoter2 + afterInflationTarget +
                        clh.feePool ==
                    clh.totalCoins);
        });

        for_versions_from(8, *app, [&] {
            clh = app->getLedgerManager().getCurrentLedgerHeader();
            REQUIRE(clh.feePool == (999999999 + 2 * 100));
            closeLedgerOn(*app, 3, 21, 7, 2014, {inflationTx});

            clh = app->getLedgerManager().getCurrentLedgerHeader();
            REQUIRE(clh.feePool == 0);
            REQUIRE(clh.totalCoins == 1000000000000000000);

            auto afterInflationRoot = root.getBalance();
            auto afterInflationVoter1 = voter1.getBalance();
            auto afterInflationVoter2 = voter2.getBalance();
            inflationTarget = loadAccount(targetKey, *app);
            auto afterInflationTarget = inflationTarget->getBalance();

            REQUIRE(beforeInflationRoot == afterInflationRoot + 100);
            REQUIRE(beforeInflationVoter1 == afterInflationVoter1);
            REQUIRE(beforeInflationVoter2 == afterInflationVoter2);
            REQUIRE(beforeInflationTarget ==
                    afterInflationTarget - (999999999 + 3 * 100));

            REQUIRE(afterInflationRoot + afterInflationVoter1 +
                        afterInflationVoter2 + afterInflationTarget +
                        clh.feePool ==
                    clh.totalCoins);
        });
    }
}
