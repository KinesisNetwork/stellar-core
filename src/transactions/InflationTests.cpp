// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SHA.h"
#include "herder/LedgerCloseData.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerState.h"
#include "ledger/LedgerStateEntry.h"
#include "ledger/LedgerStateHeader.h"
#include "lib/catch.hpp"
#include "main/Application.h"
#include "main/Config.h"
#include "test/TestAccount.h"
#include "test/TestExceptions.h"
#include "test/TestMarket.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "transactions/InflationOpFrame.h"
#include "transactions/TransactionUtils.h"
#include "util/Logging.h"
#include "util/Timer.h"
#include "util/XDROperators.h"
#include <functional>

using namespace stellar;
using namespace stellar::txtest;

static const unsigned maxWinners = 2000u;

static SecretKey
getTestAccount(int i)
{
    std::stringstream name;
    name << "A" << i;
    return getAccount(name.str().c_str());
}

static void
createTestAccounts(Application& app, int nbAccounts,
                   std::function<int64(int)> getBalance,
                   std::function<int(int)> getVote)
{
    // set up world
    auto root = TestAccount::createRoot(app);

    for (int i = 0; i < nbAccounts; i++)
    {
        int64 bal = getBalance(i);
        if (bal >= 0)
        {
            SecretKey to = getTestAccount(i);
            root.create(to, bal);

            LedgerState ls(app.getLedgerStateRoot());
            auto account = stellar::loadAccount(ls, to.getPublicKey());
            auto& ae = account.current().data.account();
            ae.inflationDest.activate() =
                getTestAccount(getVote(i)).getPublicKey();
            ls.commit();
        }
    }
}

// computes the resulting balance of each test account
static std::vector<int64>
simulateInflation(int ledgerVersion, int nbAccounts, int64& totCoins,
                  int64& totFees, std::function<int64(int)> getBalance,
                  std::function<int(int)> getVote, Application& app)
{
    std::map<int, int64> balances;
    std::map<int, int64> votes;

    std::vector<std::pair<int, int64>> votesV;

    int64 minBalance = (totCoins * 5) / 10000; // .05%

    // computes all votes
    for (int i = 0; i < nbAccounts; i++)
    {
        int64 bal = getBalance(i);
        balances[i] = bal;
        // negative balance means the account does not exist
        if (bal >= 0)
        {
            int vote = getVote(i);
            // negative means inflationdest is not set for this account
            if (vote >= 0)
            {
                votes[vote] += bal;
            }
        }
    }

    for (auto const& v : votes)
    {
        votesV.emplace_back(v);
    }

    // sort by votes, then by ID in descending order
    std::sort(
        votesV.begin(), votesV.end(),
        [](std::pair<int, int64> const& l, std::pair<int, int64> const& r) {
            if (l.second > r.second)
            {
                return true;
            }
            else if (l.second < r.second)
            {
                return false;
            }
            else
            {
                return l.first > r.first;
            }
        });

    std::vector<int> winners;
    int64 totVotes = totCoins;
    for (size_t i = 0u; i < maxWinners && i < votesV.size(); i++)
    {
        if (votesV[i].second >= minBalance)
        {
            winners.emplace_back(votesV[i].first);
        }
    }

    // 1% annual inflation on a weekly basis
    // 0.000190721
    auto inflation = bigDivide(totCoins, 190721, 1000000000, ROUND_DOWN);
    auto coinsToDole = inflation + totFees;
    int64 leftToDole = coinsToDole;

    for (auto w : winners)
    {
        // computes the share of this guy
        int64 toDoleToThis =
            bigDivide(coinsToDole, votes.at(w), totVotes, ROUND_DOWN);
        if (ledgerVersion >= 10)
        {
            LedgerState ls(app.getLedgerStateRoot());
            auto header = ls.loadHeader();
            auto winner =
                stellar::loadAccount(ls, getTestAccount(w).getPublicKey());
            toDoleToThis =
                std::min(getMaxAmountReceive(header, winner), toDoleToThis);
        }
        if (balances[w] >= 0)
        {
            balances[w] += toDoleToThis;
            if (ledgerVersion <= 7)
            {
                totCoins += toDoleToThis;
            }
            leftToDole -= toDoleToThis;
        }
    }

    if (ledgerVersion > 7)
    {
        totCoins += inflation;
    }
    totFees = leftToDole;

    std::vector<int64> balRes;
    for (auto const& b : balances)
    {
        balRes.emplace_back(b.second);
    }
    return balRes;
}

static void
doInflation(Application& app, int ledgerVersion, int nbAccounts,
            std::function<int64(int)> getBalance,
            std::function<int(int)> getVote, int expectedWinnerCount)
{
    auto getFeePool = [&] {
        LedgerState ls(app.getLedgerStateRoot());
        return ls.loadHeader().current().feePool;
    };
    auto getTotalCoins = [&] {
        LedgerState ls(app.getLedgerStateRoot());
        return ls.loadHeader().current().totalCoins;
    };

    // simulate the expected inflation based off the current ledger state
    std::map<int, int64> balances;

    // load account balances
    for (int i = 0; i < nbAccounts; i++)
    {
        if (getBalance(i) < 0)
        {
            balances[i] = -1;
            REQUIRE(!doesAccountExist(app, getTestAccount(i).getPublicKey()));
        }
        else
        {
            LedgerState ls(app.getLedgerStateRoot());
            auto account =
                stellar::loadAccount(ls, getTestAccount(i).getPublicKey());
            auto const& ae = account.current().data.account();
            balances[i] = ae.balance;
            // double check that inflationDest is setup properly
            if (ae.inflationDest)
            {
                REQUIRE(getTestAccount(getVote(i)).getPublicKey() ==
                        *ae.inflationDest);
            }
            else
            {
                REQUIRE(getVote(i) < 0);
            }
        }
    }
    REQUIRE(getFeePool() > 0);

    int64 expectedTotcoins = getTotalCoins();
    int64 expectedFees = getFeePool();

    std::vector<int64> expectedBalances;

    auto root = TestAccount::createRoot(app);
    auto txFrame = root.tx({inflation()});
    expectedFees += txFrame->getFee();

    expectedBalances = simulateInflation(
        ledgerVersion, nbAccounts, expectedTotcoins, expectedFees,
        [&](int i) { return balances[i]; }, getVote, app);

    // perform actual inflation
    applyTx(txFrame, app);

    // verify ledger state
    REQUIRE(getTotalCoins() == expectedTotcoins);
    REQUIRE(getFeePool() == expectedFees);

    // verify balances
    InflationResult const& infResult =
        getFirstResult(*txFrame).tr().inflationResult();
    auto const& payouts = infResult.payouts();
    int actualChanges = 0;

    for (int i = 0; i < nbAccounts; i++)
    {
        auto const& k = getTestAccount(i);
        if (expectedBalances[i] < 0)
        {
            REQUIRE(!doesAccountExist(app, k.getPublicKey()));
            REQUIRE(balances[i] < 0); // account didn't get deleted
        }
        else
        {
            {
                LedgerState ls(app.getLedgerStateRoot());
                auto account = stellar::loadAccount(ls, k.getPublicKey());
                auto const& ae = account.current().data.account();
                REQUIRE(expectedBalances[i] == ae.balance);
            }

            if (expectedBalances[i] != balances[i])
            {
                REQUIRE(balances[i] >= 0);
                actualChanges++;
                bool found = false;
                for (auto const& p : payouts)
                {
                    if (p.destination == k.getPublicKey())
                    {
                        int64 computedFromResult = balances[i] + p.amount;
                        REQUIRE(computedFromResult == expectedBalances[i]);
                        found = true;
                        break;
                    }
                }
                REQUIRE(found);
            }
        }
    }
    REQUIRE(actualChanges == expectedWinnerCount);
    REQUIRE(expectedWinnerCount == payouts.size());
}

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

    auto getFeePool = [&] {
        LedgerState ls(app->getLedgerStateRoot());
        return ls.loadHeader().current().feePool;
    };
    auto getInflationSeq = [&] {
        LedgerState ls(app->getLedgerStateRoot());
        return ls.loadHeader().current().inflationSeq;
    };
    auto getLedgerVersion = [&] {
        LedgerState ls(app->getLedgerStateRoot());
        return ls.loadHeader().current().ledgerVersion;
    };
    auto getTotalCoins = [&] {
        LedgerState ls(app->getLedgerStateRoot());
        return ls.loadHeader().current().totalCoins;
    };

    app->start();

    SECTION("not time")
    {
        for_all_versions(*app, [&] {
            closeLedgerOn(*app, 2, 30, 6, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);

            REQUIRE(getInflationSeq() == 0);

            closeLedgerOn(*app, 3, 1, 7, 2014);

            auto txFrame = root.tx({inflation()});

            closeLedgerOn(*app, 4, 7, 7, 2014, {txFrame});
            REQUIRE(getInflationSeq() == 1);

            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(getInflationSeq() == 1);

            closeLedgerOn(*app, 5, 8, 7, 2014);
            root.inflation();
            REQUIRE(getInflationSeq() == 2);

            closeLedgerOn(*app, 6, 14, 7, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(getInflationSeq() == 2);

            closeLedgerOn(*app, 7, 15, 7, 2014);
            root.inflation();
            REQUIRE(getInflationSeq() == 3);

            closeLedgerOn(*app, 8, 21, 7, 2014);
            REQUIRE_THROWS_AS(root.inflation(), ex_INFLATION_NOT_TIME);
            REQUIRE(getInflationSeq() == 3);
        });
    }

    SECTION("total coins")
    {
        REQUIRE(getFeePool() == 0);
        REQUIRE(getTotalCoins() == 1000000000000000000);

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
