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
#include "exchange_bittrex.h"
#include <openssl/hmac.h>

Exchange_Bittrex::Exchange_Bittrex(QByteArray pRestSign, QByteArray pRestKey)
    : Exchange(),
      isFirstAccInfo(true),
      lastTickerTime(),
      lastTradesId(0),
      lastHistoryTime(0),
      privateNonce((QDateTime::currentDateTime().toTime_t() - 1371854884) * 10),
      lastCanceledId(),
      julyHttp(nullptr),
      depthAsks(nullptr),
      depthBids(nullptr),
      lastDepthAsksMap(),
      lastDepthBidsMap()
{
    clearHistoryOnCurrencyChanged = true;
    calculatingFeeMode = 1;
    baseValues.exchangeName = "Bittrex";
    baseValues.currentPair.name = "LTC/BTC";
    baseValues.currentPair.setSymbol("LTC/BTC");
    baseValues.currentPair.currRequestPair = "BTC-LTC";
    baseValues.currentPair.priceDecimals = 3;
    minimumRequestIntervalAllowed = 500;
    baseValues.currentPair.priceMin = qPow(0.1, baseValues.currentPair.priceDecimals);
    baseValues.currentPair.tradeVolumeMin = 0.01;
    baseValues.currentPair.tradePriceMin = 0.1;
    forceDepthLoad = false;
    tickerOnly = false;
    setApiKeySecret(pRestKey, pRestSign);

    currencyMapFile = "Bittrex";
    defaultCurrencyParams.currADecimals = 8;
    defaultCurrencyParams.currBDecimals = 8;
    defaultCurrencyParams.currABalanceDecimals = 8;
    defaultCurrencyParams.currBBalanceDecimals = 8;
    defaultCurrencyParams.priceDecimals = 3;
    defaultCurrencyParams.priceMin = qPow(0.1, baseValues.currentPair.priceDecimals);

    supportsLoginIndicator = false;
    supportsAccountVolume = false;

    connect(this, &Exchange::threadFinished, this, &Exchange_Bittrex::quitThread, Qt::DirectConnection);
}

Exchange_Bittrex::~Exchange_Bittrex()
{
}

void Exchange_Bittrex::quitThread()
{
    clearValues();

    if (depthAsks)
        delete depthAsks;

    if (depthBids)
        delete depthBids;

    if (julyHttp)
        delete julyHttp;
}

void Exchange_Bittrex::clearVariables()
{
    isFirstAccInfo = true;
    lastTickerTime.clear();
    lastTradesId = 0;
    lastHistoryTime = 0;
    lastCanceledId.clear();
    Exchange::clearVariables();
    lastHistory.clear();
    lastOrders.clear();
    reloadDepth();
    emit accFeeChanged(baseValues.currentPair.symbol, 0.25);
}

void Exchange_Bittrex::clearValues()
{
    clearVariables();

    if (julyHttp)
        julyHttp->clearPendingData();
}

void Exchange_Bittrex::reloadDepth()
{
    lastDepthBidsMap.clear();
    lastDepthAsksMap.clear();
    lastDepthData.clear();
    Exchange::reloadDepth();
}

void Exchange_Bittrex::dataReceivedAuth(QByteArray data, int reqType)
{
    if (debugLevel)
        logThread->writeLog("RCV: " + data);

    if (data.size() && data.at(0) == QLatin1Char('<'))
        return;

    bool success = data.startsWith("{\"success\":true");
    QString errorString;

    if (!success)
    {
        errorString = getMidData("\"message\":\"", "\"", &data);

        if (debugLevel)
            logThread->writeLog("Invalid data:" + data, 2);
    }
    else switch (reqType)
    {
        case 103: //ticker
            {
                double tickerHigh = getMidData("\"High\":", ",", &data).toDouble();

                if (tickerHigh > 0.0 && !qFuzzyCompare(tickerHigh, lastTickerHigh))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "High", tickerHigh);
                    lastTickerHigh = tickerHigh;
                }

                double tickerLow = getMidData("\"Low\":", ",", &data).toDouble();

                if (tickerLow > 0.0 && !qFuzzyCompare(tickerLow, lastTickerLow))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Low", tickerLow);
                    lastTickerLow = tickerLow;
                }

                double tickerSell = getMidData("\"Bid\":", ",", &data).toDouble();

                if (tickerSell > 0.0 && !qFuzzyCompare(tickerSell, lastTickerSell))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Sell", tickerSell);
                    lastTickerSell = tickerSell;
                }

                double tickerBuy = getMidData("\"Ask\":", ",", &data).toDouble();

                if (tickerBuy > 0.0 && !qFuzzyCompare(tickerBuy, lastTickerBuy))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Buy", tickerBuy);
                    lastTickerBuy = tickerBuy;
                }

                double tickerVolume = getMidData("\"Volume\":", ",", &data).toDouble();

                if (tickerVolume > 0.0 && !qFuzzyCompare(tickerVolume, lastTickerVolume))
                {
                    IndicatorEngine::setValue(baseValues.exchangeName, baseValues.currentPair.symbol, "Volume", tickerVolume);
                    lastTickerVolume = tickerVolume;
                }

                QByteArray tickerTime = getMidData("\"TimeStamp\":\"", "\"", &data);

                if (tickerTime != lastTickerTime)
                {
                    lastTickerTime = tickerTime;
                    double tickerLastDouble = getMidData("\"Last\":", ",", &data).toDouble();

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
                qint64 time10Min = QDateTime::currentDateTime().toTime_t() - 600;
                QStringList tradeList = QString(data).split("},{");
                QList<TradesItem>* newTradesItems = new QList<TradesItem>;

                for (int n = tradeList.count() - 1; n >= 0; --n)
                {
                    QByteArray tradeData = tradeList.at(n).toLatin1();
                    qint64 currentTid = getMidData("\"Id\":", ",", &tradeData).toLongLong();

                    if (currentTid <= lastTradesId)
                        continue;

                    lastTradesId = currentTid;
                    TradesItem newItem;

                    QDateTime date = QDateTime::fromString(getMidData("\"TimeStamp\":\"", "\"", &tradeData), Qt::ISODate);
                    date.setTimeSpec(Qt::UTC);
                    newItem.date = date.toLocalTime().toSecsSinceEpoch();

                    if (newItem.date < time10Min)
                        continue;

                    newItem.amount = getMidData("\"Quantity\":", ",", &tradeData).toDouble();
                    newItem.price  = getMidData("\"Price\":",    ",", &tradeData).toDouble();
                    newItem.symbol = baseValues.currentPair.symbol;
                    newItem.orderType = getMidData("\"OrderType\":\"", "\"", &tradeData) == "BUY" ? 1 : -1;

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
            {
                emit depthRequestReceived();

                if (data != lastDepthData)
                {
                    lastDepthData = data;
                    depthAsks = new QList<DepthItem>;
                    depthBids = new QList<DepthItem>;

                    QMap<double, double> currentAsksMap;
                    QStringList asksList = QString(getMidData("\"sell\":[{\"Quantity\":", "}]}}", &data)).split("},{\"Quantity\":");
                    double groupedPrice = 0.0;
                    double groupedVolume = 0.0;
                    int rowCounter = 0;

                    for (int n = 0; n < asksList.count(); n++)
                    {
                        if (baseValues.depthCountLimit && rowCounter >= baseValues.depthCountLimit)
                            break;

                        QStringList currentPair = asksList.at(n).split(",\"Rate\":");

                        if (currentPair.count() != 2)
                            continue;

                        double priceDouble = currentPair.last().toDouble();
                        double amount      = currentPair.first().toDouble();

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
                    QStringList bidsList = QString(getMidData("\"buy\":[{\"Quantity\":", "}],", &data)).split("},{\"Quantity\":");
                    groupedPrice = 0.0;
                    groupedVolume = 0.0;
                    rowCounter = 0;

                    for (int n = 0; n < bidsList.count(); n++)
                    {
                        if (baseValues.depthCountLimit && rowCounter >= baseValues.depthCountLimit)
                            break;

                        QStringList currentPair = bidsList.at(n).split(",\"Rate\":");

                        if (currentPair.count() != 2)
                            continue;

                        double priceDouble = currentPair.last().toDouble();
                        double amount      = currentPair.first().toDouble();

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
            break;

        case 202: //info
            {
                QByteArray dataA = getMidData("\"Currency\":\"" + baseValues.currentPair.currAStr, "}", &data);

                if (!dataA.isEmpty())
                {
                    double btcBalance = getMidData("\"Available\":", ",", &dataA).toDouble();

                    if (btcBalance > 0.0 && !qFuzzyCompare(btcBalance, lastBtcBalance))
                    {
                        emit accBtcBalanceChanged(baseValues.currentPair.symbol, btcBalance);
                        lastBtcBalance = btcBalance;
                    }
                }

                QByteArray dataB = getMidData("\"Currency\":\"" + baseValues.currentPair.currBStr, "}", &data);

                if (!dataB.isEmpty())
                {
                    double usdBalance = getMidData("\"Available\":", ",", &dataB).toDouble();

                    if (usdBalance > 0.0 && !qFuzzyCompare(usdBalance, lastUsdBalance))
                    {
                        emit accUsdBalanceChanged(baseValues.currentPair.symbol, usdBalance);
                        lastUsdBalance = usdBalance;
                    }
                }
            }
            break;//info

        case 204://orders
            {
                if (lastOrders != data)
                {
                    lastOrders = data;

                    if (data == "{\"success\":true,\"message\":\"\",\"result\":[]}")
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

                        //0=Canceled, 1=Open, 2=Pending, 3=Post-Pending
                        if (currentOrderData.contains("\"Closed\":null"))
                            currentOrder.status = 1;
                        else
                            currentOrder.status = 0;

                        QDateTime date = QDateTime::fromString(getMidData("\"Opened\":\"", "\"", &currentOrderData), Qt::ISODate);
                        date.setTimeSpec(Qt::UTC);
                        currentOrder.date   = date.toLocalTime().toSecsSinceEpoch();
                        currentOrder.oid    = getMidData("\"OrderUuid\":\"", "\"", &currentOrderData);
                        currentOrder.type   = getMidData("\"OrderType\":\"", "\"", &currentOrderData) == "LIMIT_SELL";
                        currentOrder.amount = getMidData("\"Quantity\":",    ",",  &currentOrderData).toDouble();
                        currentOrder.price  = getMidData("\"Limit\":",       ",",  &currentOrderData).toDouble();
                        QList<QByteArray> p = getMidData("\"Exchange\":\"",  "\"", &currentOrderData).split('-');
                        currentOrder.symbol = p.last() + '/' + p.first();

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
            if (!lastCanceledId.isEmpty())
            {
                emit orderCanceled(baseValues.currentPair.symbol, lastCanceledId);
                lastCanceledId.clear();
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
                if (data.size() < 50)
                    break;

                if (lastHistory != data)
                {
                    lastHistory = data;

                    QStringList historyList = QString(data).split("},{");
                    qint64 maxTime = 0;
                    QList<HistoryItem>* historyItems = new QList<HistoryItem>;

                    for (int n = 0; n < historyList.count(); ++n)
                    {
                        QByteArray logData(historyList.at(n).toLatin1());

                        QDateTime date = QDateTime::fromString(getMidData("\"Closed\":\"", "\"", &logData), Qt::ISODate);
                        date.setTimeSpec(Qt::UTC);
                        qint64 dateInt = date.toLocalTime().toMSecsSinceEpoch();

                        if (dateInt <= lastHistoryTime)
                            break;

                        if (dateInt > maxTime)
                            maxTime = dateInt;

                        HistoryItem currentHistoryItem;

                        if (getMidData("\"OrderType\":\"", "\"", &logData) == "LIMIT_SELL")
                            currentHistoryItem.type = 1;
                        else
                            currentHistoryItem.type = 2;

                        QList<QByteArray> pair         = getMidData("\"Exchange\":\"",   "\"", &logData).split('-');
                        currentHistoryItem.symbol      = pair.last() + '/' + pair.first();
                        currentHistoryItem.price       = getMidData("\"PricePerUnit\":", ",",  &logData).toDouble();
                        currentHistoryItem.volume      = getMidData("\"Quantity\":",     ",",  &logData).toDouble();
                        currentHistoryItem.dateTimeInt = dateInt / 1000;

                        if (currentHistoryItem.isValid())
                            (*historyItems) << currentHistoryItem;
                    }

                    if (maxTime > lastHistoryTime)
                        lastHistoryTime = maxTime;

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

void Exchange_Bittrex::depthUpdateOrder(QString symbol, double price, double amount, bool isAsk)
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

void Exchange_Bittrex::depthSubmitOrder(QString symbol, QMap<double, double>* currentMap, double priceDouble,
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

bool Exchange_Bittrex::isReplayPending(int reqType)
{
    if (julyHttp == nullptr)
        return false;

    return julyHttp->isReqTypePending(reqType);
}

void Exchange_Bittrex::secondSlot()
{
    static int sendCounter = 0;

    switch (sendCounter)
    {
        case 0:
            if (!isReplayPending(103))
                sendToApi(103, "getmarketsummary?market=" + baseValues.currentPair.currRequestPair);

            break;

        case 1:
            if (!isReplayPending(202))
                sendToApi(202, "account/getbalances?", true);

            break;

        case 2:
            if (!isReplayPending(109))
                sendToApi(109, "getmarkethistory?market=" + baseValues.currentPair.currRequestPair);

            break;

        case 3:
            if (!tickerOnly && !isReplayPending(204))
                sendToApi(204, "market/getopenorders?market=" + baseValues.currentPair.currRequestPair + "&", true);

            break;

        case 4:
            if (isDepthEnabled() && (forceDepthLoad || !isReplayPending(111)))
            {
                emit depthRequested();
                sendToApi(111, "getorderbook?type=both&market=" + baseValues.currentPair.currRequestPair);
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

void Exchange_Bittrex::getHistory(bool force)
{
    if (tickerOnly)
        return;

    if (force)
        lastHistory.clear();

    if (!isReplayPending(208))
        sendToApi(208, "account/getorderhistory?market=" + baseValues.currentPair.currRequestPair + "&", true);
}

void Exchange_Bittrex::buy(QString symbol, double apiBtcToBuy, double apiPriceToBuy)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "market=" + pairItem.currRequestPair + "&quantity=" +
            JulyMath::byteArrayFromDouble(apiBtcToBuy, pairItem.currADecimals, 0) + "&rate=" +
            JulyMath::byteArrayFromDouble(apiPriceToBuy, pairItem.priceDecimals, 0) + "&";

    if (debugLevel)
        logThread->writeLog("Buy: " + data, 2);

    sendToApi(306, "market/buylimit?" + data, true);
}

void Exchange_Bittrex::sell(QString symbol, double apiBtcToSell, double apiPriceToSell)
{
    if (tickerOnly)
        return;

    CurrencyPairItem pairItem;
    pairItem = baseValues.currencyPairMap.value(symbol, pairItem);

    if (pairItem.symbol.isEmpty())
        return;

    QByteArray data = "market=" + pairItem.currRequestPair + "&quantity=" +
            JulyMath::byteArrayFromDouble(apiBtcToSell, pairItem.currADecimals, 0) + "&rate=" +
            JulyMath::byteArrayFromDouble(apiPriceToSell, pairItem.priceDecimals, 0) + "&";

    if (debugLevel)
        logThread->writeLog("Sell: " + data, 2);

    sendToApi(306, "market/selllimit?" + data, true);
}

void Exchange_Bittrex::cancelOrder(QString, QByteArray order)
{
    if (tickerOnly)
        return;

    lastCanceledId  = order;
    QByteArray data = "uuid=" + order + "&";

    if (debugLevel)
        logThread->writeLog("Cancel order: " + data, 2);

    sendToApi(305, "market/cancel?" + data, true);
}

void Exchange_Bittrex::sendToApi(int reqType, QByteArray method, bool auth)
{
    if (julyHttp == nullptr)
    {
        julyHttp = new JulyHttp("bittrex.com", "apisign:", this);
        connect(julyHttp, SIGNAL(anyDataReceived()), baseValues_->mainWindow_, SLOT(anyDataReceived()));
        connect(julyHttp, SIGNAL(apiDown(bool)), baseValues_->mainWindow_, SLOT(setApiDown(bool)));
        connect(julyHttp, SIGNAL(setDataPending(bool)), baseValues_->mainWindow_, SLOT(setDataPending(bool)));
        connect(julyHttp, SIGNAL(errorSignal(QString)), baseValues_->mainWindow_, SLOT(showErrorMessage(QString)));
        connect(julyHttp, SIGNAL(sslErrorSignal(const QList<QSslError>&)), this, SLOT(sslErrors(const QList<QSslError>&)));
        connect(julyHttp, SIGNAL(dataReceived(QByteArray, int)), this, SLOT(dataReceivedAuth(QByteArray, int)));
    }

    if (auth)
    {
        QByteArray path = "/api/v1.1/" + method + "apikey=" + getApiKey() + "&nonce=" + QByteArray::number(++privateNonce);
        QByteArray url  = "https://bittrex.com" + path;
        QByteArray sign = hmacSha512(getApiSign(), url).toHex();

        julyHttp->sendData(reqType, "GET " + path, "", sign + "\r\n\r\n");
    }
    else
    {
        julyHttp->sendData(reqType, "GET /api/v1.1/public/" + method);
    }
}

void Exchange_Bittrex::sslErrors(const QList<QSslError>& errors)
{
    QStringList errorList;

    for (int n = 0; n < errors.count(); n++)
        errorList << errors.at(n).errorString();

    if (debugLevel)
        logThread->writeLog(errorList.join(" ").toLatin1(), 2);

    emit showErrorMessage("SSL Error: " + errorList.join(" "));
}
