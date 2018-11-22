//  This file is part of Qt Bitcoin Trader
//      https://github.com/JulyIGHOR/QtBitcoinTrader
//  Copyright (C) 2013-2018 July IGHOR <julyighor@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  In addition, as a special exception, the copyright holders give
//  permission to link the code of portions of this program with the
//  OpenSSL library under certain conditions as described in each
//  individual source file, and distribute linked combinations including
//  the two.
//
//  You must obey the GNU General Public License in all respects for all
//  of the code used other than OpenSSL. If you modify file(s) with this
//  exception, you may extend this exception to your version of the
//  file(s), but you are not obligated to do so. If you do not wish to do
//  so, delete this exception statement from your version. If you delete
//  this exception statement from all source files in the program, then
//  also delete it here.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "iniengine.h"
#include "exchange_binance.h"
#include <openssl/hmac.h>

Exchange_Binance::Exchange_Binance(QByteArray pRestSign, QByteArray pRestKey)
    : Exchange(),
      isFirstAccInfo(true),
      sslErrorCounter(0),
      lastTickerId(0),
      lastTradesId(0),
      lastHistoryId(0),
      julyHttp(nullptr),
      depthAsks(nullptr),
      depthBids(nullptr),
      lastDepthAsksMap(),
      lastDepthBidsMap()
{
    clearHistoryOnCurrencyChanged = true;
    calculatingFeeMode = 1;
    baseValues.exchangeName = "Binance";
    baseValues.currentPair.name = "BTC/USD";
    baseValues.currentPair.setSymbol("BTCUSD");
    baseValues.currentPair.currRequestPair = "btc_usd";
    baseValues.currentPair.priceDecimals = 3;
    minimumRequestIntervalAllowed = 500;
    baseValues.currentPair.priceMin = qPow(0.1, baseValues.currentPair.priceDecimals);
    baseValues.currentPair.tradeVolumeMin = 0.01;
    baseValues.currentPair.tradePriceMin = 0.1;
    forceDepthLoad = false;
    tickerOnly = false;
    setApiKeySecret(pRestKey, pRestSign);

    currencyMapFile = "Binance";
    defaultCurrencyParams.currADecimals = 8;
    defaultCurrencyParams.currBDecimals = 8;
    defaultCurrencyParams.currABalanceDecimals = 8;
    defaultCurrencyParams.currBBalanceDecimals = 8;
    defaultCurrencyParams.priceDecimals = 3;
    defaultCurrencyParams.priceMin = qPow(0.1, baseValues.currentPair.priceDecimals);

    supportsLoginIndicator = false;
    supportsAccountVolume = false;

    connect(this, &Exchange::threadFinished, this, &Exchange_Binance::quitThread, Qt::DirectConnection);
}

Exchange_Binance::~Exchange_Binance()
{
}

void Exchange_Binance::quitThread()
{
    clearValues();

    if (depthAsks)
        delete depthAsks;

    if (depthBids)
        delete depthBids;

    if (julyHttp)
        delete julyHttp;
}

void Exchange_Binance::clearVariables()
{
    isFirstAccInfo = true;
    lastTickerId = 0;
    lastTradesId = 0;
    lastHistoryId = 0;
    Exchange::clearVariables();
    lastHistory.clear();
    lastOrders.clear();
    reloadDepth();
}

void Exchange_Binance::clearValues()
{
    clearVariables();

    if (julyHttp)
        julyHttp->clearPendingData();
}

void Exchange_Binance::reloadDepth()
{
    lastDepthBidsMap.clear();
    lastDepthAsksMap.clear();
    lastDepthData.clear();
    Exchange::reloadDepth();
}

void Exchange_Binance::dataReceivedAuth(QByteArray data, int reqType)
{
    sslErrorCounter = 0;

    if (debugLevel)
        logThread->writeLog("RCV: " + data);

    if (data.size() && data.at(0) == QLatin1Char('<'))
        return;

    bool success = !data.startsWith("{\"success\":0");
    QString errorString;

    if (!success)
        errorString = getMidData("msg\":\"", "\"", &data);


    switch (reqType)
    {
        case 103: //ticker
            {
                double tickerHigh = getMidData("\"highPrice\":\"", "\"", &data).toDouble();

                if (tickerHigh > 0.0 && !qFuzzyCompare(tickerHigh, lastTickerHigh))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "High", tickerHigh);
                    lastTickerHigh = tickerHigh;
                }

                double tickerLow = getMidData("\"lowPrice\":\"", "\"", &data).toDouble();

                if (tickerLow > 0.0 && !qFuzzyCompare(tickerLow, lastTickerLow))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Low", tickerLow);
                    lastTickerLow = tickerLow;
                }

                double tickerSell = getMidData("\"bidPrice\":\"", "\"", &data).toDouble();

                if (tickerSell > 0.0 && !qFuzzyCompare(tickerSell, lastTickerSell))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Sell", tickerSell);
                    lastTickerSell = tickerSell;
                }

                double tickerBuy = getMidData("\"askPrice\":\"", "\"", &data).toDouble();

                if (tickerBuy > 0.0 && !qFuzzyCompare(tickerBuy, lastTickerBuy))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Buy", tickerBuy);
                    lastTickerBuy = tickerBuy;
                }

                double tickerVolume = getMidData("\"volume\":\"", "\"", &data).toDouble();

                if (tickerVolume > 0.0 && !qFuzzyCompare(tickerVolume, lastTickerVolume))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Volume", tickerVolume);
                    lastTickerVolume = tickerVolume;
                }

                qint64 tickerId = getMidData("\"lastId\":", ",", &data).toLongLong();

                if (tickerId > lastTickerId)
                {
                    lastTickerId = tickerId;
                    double tickerLastDouble = getMidData("\"lastPrice\":\"", "\"", &data).toDouble();

                    if (tickerLastDouble > 0.0 && !qFuzzyCompare(tickerLastDouble, lastTickerLast))
                    {
                        IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Last", tickerLastDouble);
                        lastTickerLast = tickerLastDouble;
                    }
                }
            }
            break;//ticker

        case 109: //trades
            {
                if (data.size() < 10)
                    break;

                qint64 time10Min = QDateTime::currentDateTime().toTime_t() - 600;
                QStringList tradeList = QString(data).split("},{");
                QList<TradesItem>* newTradesItems = new QList<TradesItem>;
                int lastIndex = tradeList.count() - 1;

                for (int n = 0; n < tradeList.count(); ++n)
                {
                    QByteArray tradeData = tradeList.at(n).toLatin1() + "}";
                    TradesItem newItem;
                    qint64 currentTid = getMidData("\"id\":", ",", &tradeData).toLongLong();

                    if (currentTid <= lastTradesId)
                        continue;

                    lastTradesId = currentTid;
                    newItem.date = getMidData("\"time\":", ",", &tradeData).toLongLong() / 1000;

                    if (newItem.date < time10Min)
                        continue;

                    newItem.price = getMidData("\"price\":\"", "\"", &tradeData).toDouble();

                    if (n == lastIndex && currentTid > lastTickerId)
                    {
                        lastTickerId = currentTid;

                        if (newItem.price > 0.0 && !qFuzzyCompare(newItem.price, lastTickerLast))
                        {
                            IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Last", newItem.price);
                            lastTickerLast = newItem.price;
                        }
                    }

                    newItem.amount = getMidData("\"qty\":\"", "\"", &tradeData).toDouble();
                    newItem.symbol = baseValues.currentPair.symbol;
                    newItem.orderType = getMidData("\"isBuyerMaker\":", ",", &tradeData) == "true" ? 1 : -1;

                    if (newItem.isValid())
                        (*newTradesItems) << newItem;
                    else if (debugLevel)
                        logThread->writeLog("Invalid trades fetch data line:" + tradeData, 2);
                }

                if (newTradesItems->count())
                    emit addLastTrades(baseValues.currentPair.symbol, newTradesItems);
                else
                    delete newTradesItems;
            }
            break;//trades

        case 111: //depth
            if (data.startsWith("{\"lastUpdateId\":"))
            {
                emit depthRequestReceived();
                QByteArray lastUpdateId = getMidData("\"lastUpdateId\":", ",", &data);

                if (lastUpdateId != lastDepthData)
                {
                    lastDepthData = lastUpdateId;
                    depthAsks = new QList<DepthItem>;
                    depthBids = new QList<DepthItem>;

                    QMap<double, double> currentAsksMap;
                    QStringList asksList = QString(getMidData("\"asks\":[[\"", "\",[]]]", &data)).split("\",[]],[\"");
                    double groupedPrice = 0.0;
                    double groupedVolume = 0.0;
                    int rowCounter = 0;

                    for (int n = 0; n < asksList.count(); n++)
                    {
                        if (baseValues.depthCountLimit && rowCounter >= baseValues.depthCountLimit)
                            break;

                        QStringList currentPair = asksList.at(n).split("\",\"");

                        if (currentPair.count() != 2)
                            continue;

                        double priceDouble = currentPair.first().toDouble();
                        double amount = currentPair.last().toDouble();

                        if (baseValues.groupPriceValue > 0.0)
                        {
                            if (n == 0)
                            {
                                emit depthFirstOrder(baseValues.currentPair.symbol, priceDouble, amount, true);
                                groupedPrice = baseValues.groupPriceValue * static_cast<int>(priceDouble / baseValues.groupPriceValue);
                                groupedVolume = amount;
                            }
                            else
                            {
                                bool matchCurrentGroup = priceDouble < groupedPrice + baseValues.groupPriceValue;

                                if (matchCurrentGroup)
                                    groupedVolume += amount;

                                if (!matchCurrentGroup || n == asksList.count() - 1)
                                {
                                    depthSubmitOrder(baseValues.currentPair.symbol,
                                                     &currentAsksMap, groupedPrice + baseValues.groupPriceValue, groupedVolume, true);
                                    rowCounter++;
                                    groupedVolume = amount;
                                    groupedPrice += baseValues.groupPriceValue;
                                }
                            }
                        }
                        else
                        {
                            depthSubmitOrder(baseValues.currentPair.symbol,
                                             &currentAsksMap, priceDouble, amount, true);
                            rowCounter++;
                        }
                    }

                    QList<double> currentAsksList = lastDepthAsksMap.keys();

                    for (int n = 0; n < currentAsksList.count(); n++)
                        if (qFuzzyIsNull(currentAsksMap.value(currentAsksList.at(n), 0)))
                            depthUpdateOrder(baseValues.currentPair.symbol,
                                             currentAsksList.at(n), 0.0, true);

                    lastDepthAsksMap = currentAsksMap;

                    QMap<double, double> currentBidsMap;
                    QStringList bidsList = QString(getMidData("\"bids\":[[\"", "\",[]]]", &data)).split("\",[]],[\"");
                    groupedPrice = 0.0;
                    groupedVolume = 0.0;
                    rowCounter = 0;

                    for (int n = 0; n < bidsList.count(); n++)
                    {
                        if (baseValues.depthCountLimit && rowCounter >= baseValues.depthCountLimit)
                            break;

                        QStringList currentPair = bidsList.at(n).split("\",\"");

                        if (currentPair.count() != 2)
                            continue;

                        double priceDouble = currentPair.first().toDouble();
                        double amount = currentPair.last().toDouble();

                        if (baseValues.groupPriceValue > 0.0)
                        {
                            if (n == 0)
                            {
                                emit depthFirstOrder(baseValues.currentPair.symbol, priceDouble, amount, false);
                                groupedPrice = baseValues.groupPriceValue * static_cast<int>(priceDouble / baseValues.groupPriceValue);
                                groupedVolume = amount;
                            }
                            else
                            {
                                bool matchCurrentGroup = priceDouble > groupedPrice - baseValues.groupPriceValue;

                                if (matchCurrentGroup)
                                    groupedVolume += amount;

                                if (!matchCurrentGroup || n == asksList.count() - 1)
                                {
                                    depthSubmitOrder(baseValues.currentPair.symbol,
                                                     &currentBidsMap, groupedPrice - baseValues.groupPriceValue, groupedVolume, false);
                                    rowCounter++;
                                    groupedVolume = amount;
                                    groupedPrice -= baseValues.groupPriceValue;
                                }
                            }
                        }
                        else
                        {
                            depthSubmitOrder(baseValues.currentPair.symbol,
                                             &currentBidsMap, priceDouble, amount, false);
                            rowCounter++;
                        }
                    }

                    QList<double> currentBidsList = lastDepthBidsMap.keys();

                    for (int n = 0; n < currentBidsList.count(); n++)
                        if (qFuzzyIsNull(currentBidsMap.value(currentBidsList.at(n), 0)))
                            depthUpdateOrder(baseValues.currentPair.symbol,
                                             currentBidsList.at(n), 0.0, false);

                    lastDepthBidsMap = currentBidsMap;

                    emit depthSubmitOrders(baseValues.currentPair.symbol, depthAsks, depthBids);
                    depthAsks = nullptr;
                    depthBids = nullptr;
                }
            }
            else if (debugLevel)
                logThread->writeLog("Invalid depth data:" + data, 2);

            break;

        case 202: //info
            {
                if (!success)
                    break;

                QByteArray fundsData = getMidData("\"balances\":[{", "}]}", &data);
                double btcBalance = getMidData("\"" + baseValues.currentPair.currAStr + "\",\"free\":\"", "\"", &fundsData).toDouble();

                if (btcBalance > 0.0 && !qFuzzyCompare(btcBalance, lastBtcBalance))
                {
                    emit accBtcBalanceChanged(baseValues.currentPair.symbol, btcBalance);
                    lastBtcBalance = btcBalance;
                }

                double usdBalance = getMidData("\"" + baseValues.currentPair.currBStr + "\",\"free\":\"", "\"", &fundsData).toDouble();

                if (usdBalance > 0.0 && !qFuzzyCompare(usdBalance, lastUsdBalance))
                {
                    emit accUsdBalanceChanged(baseValues.currentPair.symbol, usdBalance);
                    lastUsdBalance = usdBalance;
                }

                double fee = qMax(getMidData("\"makerCommission\":", ",", &data).toDouble(),
                                  getMidData("\"takerCommission\":", ",", &data).toDouble()) / 100;

                if (!qFuzzyCompare(fee + 1.0, lastFee + 1.0))
                {
                    emit accFeeChanged(baseValues.currentPair.symbol, fee);
                    lastFee = fee;
                }

                if (isFirstAccInfo)
                {
                    QByteArray rights = getMidData("\"canTrade\":", ",", &data);

                    if (!rights.isEmpty())
                    {
                        if (rights != "true")
                            emit showErrorMessage("I:>invalid_rights");

                        isFirstAccInfo = false;
                    }
                }
            }
            break;//info

        case 204://orders
            {
                if (lastOrders != data)
                {
                    lastOrders = data;

                    if (data == "[]")
                    {
                        emit ordersIsEmpty();
                        break;
                    }

                    QStringList ordersList = QString(data).split("},{");
                    QList<OrderItem>* orders = new QList<OrderItem>;

                    for (int n = 0; n < ordersList.count(); ++n)
                    {
                        OrderItem currentOrder;
                        QByteArray currentOrderData = ordersList.at(n).toLatin1();
                        QByteArray status = getMidData("status\":\"", "\"", &currentOrderData);

                        //0=Canceled, 1=Open, 2=Pending, 3=Post-Pending
                        if (status == "CANCELED" || status == "REJECTED" || status == "EXPIRED")
                            currentOrder.status = 0;
                        else if (status == "NEW" || status == "PARTIALLY_FILLED")
                            currentOrder.status = 1;
                        else
                            currentOrder.status = 2;

                        QByteArray date     = getMidData("\"time\":",      ",",  &currentOrderData);
                        date.chop(3);
                        currentOrder.date   = date.toUInt();
                        currentOrder.oid    = getMidData("\"orderId\":",   ",",  &currentOrderData);
                        currentOrder.type   = getMidData("\"side\":\"",    "\"", &currentOrderData) == "SELL";
                        currentOrder.amount = getMidData("\"origQty\":\"", "\"", &currentOrderData).toDouble();
                        currentOrder.price  = getMidData("\"price\":\"",   "\"", &currentOrderData).toDouble();
                        QByteArray request  = getMidData("\"symbol\":\"",  "\"", &currentOrderData);
                        QList<CurrencyPairItem>* pairs = IniEngine::getPairs();

                        for (int i = 0; i < pairs->count(); ++i)
                        {
                            if (pairs->at(i).currRequestPair == request)
                            {
                                currentOrder.symbol = pairs->at(i).symbol;
                                break;
                            }
                        }

                        if (currentOrder.isValid())
                            (*orders) << currentOrder;
                    }

                    if (orders->count())
                        emit orderBookChanged(baseValues.currentPair.symbol, orders);
                    else
                        delete orders;
                }

                break;//orders
            }

        case 305: //order/cancel
            if (success)
            {
                QByteArray oid = getMidData("\"orderId\":", ",", &data);

                if (!oid.isEmpty())
                    emit orderCanceled(baseValues.currentPair.symbol, oid);
            }

            break;//order/cancel

        case 306:
            if (debugLevel)
                logThread->writeLog("Buy OK: " + data, 2);

            break;//order/buy

        case 307:
            if (debugLevel)
                logThread->writeLog("Sell OK: " + data, 2);

            break;//order/sell

        case 208: //history
            {
                if (data.size() < 10)
                    break;

                if (lastHistory != data)
                {
                    lastHistory = data;

                    QStringList historyList = QString(data).split("},{");
                    qint64 maxId = 0;
                    QList<HistoryItem>* historyItems = new QList<HistoryItem>;

                    for (int n = historyList.count() - 1; n >= 0; --n)
                    {
                        QByteArray logData(historyList.at(n).toLatin1());
                        qint64 id = getMidData("\"id\":", ",", &logData).toLongLong();

                        if (id <= lastHistoryId)
                            break;

                        if (id > maxId)
                            maxId = id;

                        HistoryItem currentHistoryItem;

                        if (getMidData("\"isBuyer\":", ",", &logData) == "true")
                            currentHistoryItem.type = 2;
                        else
                            currentHistoryItem.type = 1;

                        QByteArray request  = getMidData("\"symbol\":\"", "\"", &logData);
                        QList<CurrencyPairItem>* pairs = IniEngine::getPairs();

                        for (int i = 0; i < pairs->count(); ++i)
                        {
                            if (pairs->at(i).currRequestPair == request)
                            {
                                currentHistoryItem.symbol = pairs->at(i).symbol;
                                break;
                            }
                        }

                        QByteArray data                = getMidData("\"time\":",    ",",  &logData);
                        data.chop(3);
                        currentHistoryItem.dateTimeInt = data.toUInt();
                        currentHistoryItem.price       = getMidData("\"price\":\"", "\"", &logData).toDouble();
                        currentHistoryItem.volume      = getMidData("\"qty\":\"",   "\"", &logData).toDouble();

                        if (currentHistoryItem.isValid())
                            (*historyItems) << currentHistoryItem;
                    }

                    if (maxId > lastHistoryId)
                        lastHistoryId = maxId;

                    emit historyChanged(historyItems);
                }

                break;//money/wallet/history
            }

        default:
            break;
    }

    if (reqType >= 200 && reqType < 300)
    {
        static int authErrorCount = 0;

        if (!success)
        {
            authErrorCount++;

            if (authErrorCount < 3)
                return;

            if (debugLevel)
                logThread->writeLog("API error: " + errorString.toLatin1() + " ReqType: " + QByteArray::number(reqType), 2);

            if (!errorString.isEmpty())
                emit showErrorMessage(errorString);
        }
        else
            authErrorCount = 0;
    }
    else if (reqType < 200)
    {
        static int errorCount = 0;

        if (!success)
        {
            errorCount++;

            if (errorCount < 3)
                return;

            if (debugLevel)
                logThread->writeLog("API error: " + errorString.toLatin1() + " ReqType: " + QByteArray::number(reqType), 2);

            if (!errorString.isEmpty())
                emit showErrorMessage("I:>" + errorString);
        }
        else
            errorCount = 0;
    }
}

void Exchange_Binance::depthUpdateOrder(QString symbol, double price, double amount, bool isAsk)
{
    if (symbol != baseValues.currentPair.symbol)
        return;

    if (isAsk)
    {
        if (depthAsks == nullptr)
            return;

        DepthItem newItem;
        newItem.price = price;
        newItem.volume = amount;

        if (newItem.isValid())
            (*depthAsks) << newItem;
    }
    else
    {
        if (depthBids == nullptr)
            return;

        DepthItem newItem;
        newItem.price = price;
        newItem.volume = amount;

        if (newItem.isValid())
            (*depthBids) << newItem;
    }
}

void Exchange_Binance::depthSubmitOrder(QString symbol, QMap<double, double>* currentMap, double priceDouble,
                                    double amount, bool isAsk)
{
    if (symbol != baseValues.currentPair.symbol)
        return;

    if (priceDouble == 0.0 || amount == 0.0)
        return;

    if (isAsk)
    {
        (*currentMap)[priceDouble] = amount;

        if (!qFuzzyCompare(lastDepthAsksMap.value(priceDouble, 0.0), amount))
            depthUpdateOrder(symbol, priceDouble, amount, true);
    }
    else
    {
        (*currentMap)[priceDouble] = amount;

        if (!qFuzzyCompare(lastDepthBidsMap.value(priceDouble, 0.0), amount))
            depthUpdateOrder(symbol, priceDouble, amount, false);
    }
}

bool Exchange_Binance::isReplayPending(int reqType)
{
    if (julyHttp == nullptr)
        return false;

    return julyHttp->isReqTypePending(reqType);
}

void Exchange_Binance::secondSlot()
{
    static int sendCounter = 0;

    switch (sendCounter)
    {
        case 0:
            if (!isReplayPending(103))
                sendToApi(103, "v1/ticker/24hr?symbol=" + baseValues.currentPair.currRequestPair, false, true);

            break;

        case 1:
            if (!isReplayPending(202))
                sendToApi(202, "GET /api/v3/account?", true, true);

            break;

        case 2:
            if (!isReplayPending(109))
            {
                QByteArray fromId = lastTradesId ? "&fromId=" + QByteArray::number(lastTradesId + 1) : "";
                sendToApi(109, "v1/historicalTrades?symbol=" + baseValues.currentPair.currRequestPair + fromId, false, false);
            }

            break;

        case 3:
            if (!tickerOnly && !isReplayPending(204))
                sendToApi(204, "GET /api/v3/openOrders?", true, true/*, "symbol=" + baseValues.currentPair.currRequestPair + "&"*/);

            break;

        case 4:
            if (isDepthEnabled() && (forceDepthLoad || !isReplayPending(111)))
            {
                emit depthRequested();
                sendToApi(111, "v1/depth?symbol=" + baseValues.currentPair.currRequestPair + "&limit=" + baseValues.depthCountLimitStr, false, true);
                forceDepthLoad = false;
            }

            break;

        case 5:
            if (lastHistory.isEmpty())
                getHistory(false);

            break;

        default:
            break;
    }

    if (sendCounter++ >= 5)
        sendCounter = 0;

    Exchange::secondSlot();
}

void Exchange_Binance::getHistory(bool force)
{
    if (tickerOnly)
        return;

    if (force)
        lastHistory.clear();

    if (!isReplayPending(208))
    {
        QByteArray fromId = lastHistoryId ? "fromId=" + QByteArray::number(lastHistoryId + 1) + "&" : "";
        sendToApi(208, "GET /api/v3/myTrades?", true, true, "symbol=" + baseValues.currentPair.currRequestPair + "&" + fromId);
    }
}

void Exchange_Binance::buy(QString symbol, double apiBtcToBuy, double apiPriceToBuy)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "symbol=" + pairItem.currRequestPair + "&side=BUY&type=LIMIT&timeInForce=GTC&quantity=" +
            JulyMath::byteArrayFromDouble(apiBtcToBuy, pairItem.currADecimals, 0) + "&price=" +
            JulyMath::byteArrayFromDouble(apiPriceToBuy, pairItem.priceDecimals, 0) + "&";

    if (debugLevel)
        logThread->writeLog("Buy: " + data, 2);

    sendToApi(306, "POST /api/v3/order?", true, true, data);
}

void Exchange_Binance::sell(QString symbol, double apiBtcToSell, double apiPriceToSell)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "symbol=" + pairItem.currRequestPair + "&side=SELL&type=LIMIT&timeInForce=GTC&quantity=" +
            JulyMath::byteArrayFromDouble(apiBtcToSell, pairItem.currADecimals, 0) + "&price=" +
            JulyMath::byteArrayFromDouble(apiPriceToSell, pairItem.priceDecimals, 0) + "&";

    if (debugLevel)
        logThread->writeLog("Sell: " + data, 2);

    sendToApi(307, "POST /api/v3/order?", true, true, data);
}

void Exchange_Binance::cancelOrder(QString symbol, QByteArray order)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "symbol=" + pairItem.currRequestPair + "&orderId=" + order + "&";

    if (debugLevel)
        logThread->writeLog("Cancel order: " + data, 2);

    sendToApi(305, "DELETE /api/v3/order?", true, true, data);
}

void Exchange_Binance::sendToApi(int reqType, QByteArray method, bool auth, bool simple, QByteArray commands)
{
    if (julyHttp == nullptr)
    {
        julyHttp = new JulyHttp("api.binance.com", "X-MBX-APIKEY: " + getApiKey() + "\r", this);
        connect(julyHttp, SIGNAL(anyDataReceived()), baseValues_->mainWindow_, SLOT(anyDataReceived()));
        connect(julyHttp, SIGNAL(apiDown(bool)), baseValues_->mainWindow_, SLOT(setApiDown(bool)));
        connect(julyHttp, SIGNAL(setDataPending(bool)), baseValues_->mainWindow_, SLOT(setDataPending(bool)));
        connect(julyHttp, SIGNAL(errorSignal(QString)), baseValues_->mainWindow_, SLOT(showErrorMessage(QString)));
        connect(julyHttp, SIGNAL(sslErrorSignal(const QList<QSslError>&)), this, SLOT(sslErrors(const QList<QSslError>&)));
        connect(julyHttp, SIGNAL(dataReceived(QByteArray, int)), this, SLOT(dataReceivedAuth(QByteArray, int)));
    }

    if (auth)
    {
        QByteArray data = commands + "recvWindow=30000&timestamp=" +
                QByteArray::number(QDateTime::currentDateTime().toMSecsSinceEpoch() - 10000);
        julyHttp->sendData(reqType, method + data + "&signature=" + hmacSha256(getApiSign(), data).toHex(), "", "\n\r\n");
    }
    else
    {
        if (simple)
            julyHttp->sendData(reqType, "GET /api/" + method);
        else
            julyHttp->sendData(reqType, "GET /api/" + method, "", "\n\r\n");
    }
}

void Exchange_Binance::sslErrors(const QList<QSslError>& errors)
{
    if (++sslErrorCounter < 3)
        return;

    QStringList errorList;

    for (int n = 0; n < errors.count(); n++)
        errorList << errors.at(n).errorString();

    if (debugLevel)
        logThread->writeLog(errorList.join(" ").toLatin1(), 2);

    emit showErrorMessage("SSL Error: " + errorList.join(" "));
}
