#include "youkuprovider.h"
#include "Common/htmlparsersax.h"
#include "Common/network.h"
namespace
{
    const char *supportedUrlRe[]={"(https?://)?v\\.youku\\.com/v_show/id_[a-zA-Z0-9]+(==)?(.html)?",
                                  "(https?://)?v\\.youku\\.com/v_nextstage/id_[a-zA-Z0-9]+(==)?(.html)?"};
}

QStringList YoukuProvider::supportedURLs()
{
    return QStringList({"https://v.youku.com/v_show/id_XMTI3ODI4OTU1Ng",
                       "https://v.youku.com/v_nextstage/id_f82c894261ad11e0bea1.html"});
}

DanmuAccessResult *YoukuProvider::search(const QString &keyword)
{
    QString baseUrl = QString("https://so.youku.com/search_video/q_%1").arg(keyword);
    DanmuAccessResult *searchResult=new DanmuAccessResult;
    searchResult->providerId=id();
    try
    {
		QString str(Network::httpGet(baseUrl, QUrlQuery(), QStringList({ "Cookie","cna=0;" })));
        handleSearchReply(str,searchResult);
    }
    catch(Network::NetworkError &error)
    {
        searchResult->error=true;
        searchResult->errorInfo=error.errorInfo;
    }
    emit searchDone(searchResult);
    return searchResult;
}

DanmuAccessResult *YoukuProvider::getEpInfo(DanmuSourceItem *item)
{
    DanmuAccessResult *result=new DanmuAccessResult;
    result->providerId=id();
    result->error=false;
    result->list.append(*item);
    emit epInfoDone(result,item);
    return result;
}

DanmuAccessResult *YoukuProvider::getURLInfo(const QString &url)
{
    int reCount=sizeof(supportedUrlRe)/sizeof(const char *);
    int matchIndex=0;
    for(;matchIndex<reCount;++matchIndex)
    {
        QRegExp re(supportedUrlRe[matchIndex]);
        re.indexIn(url);
        if(re.matchedLength()==url.length())
            break;
    }
    if(matchIndex==reCount)
    {
        return nullptr;
    }
    DanmuSourceItem item;
    item.strId=url;
    return getEpInfo(&item);
}

QString YoukuProvider::downloadDanmu(DanmuSourceItem *item, QList<DanmuComment *> &danmuList)
{
    QString errInfo;
    try
    {
        QString replyStr(Network::httpGet(item->strId,QUrlQuery()));
        QRegExp re;
        re.setMinimal(true);
        bool hasError=true;
        do
        {
            re.setPattern("\\bvideoId: '(\\d+)'");
            int pos=re.indexIn(replyStr);
            if(pos==-1) break;
            QStringList captured=re.capturedTexts();
            item->strId=captured[1];
            re.setPattern("\\bseconds: '([\\d\\.]+)'");
            pos=re.indexIn(replyStr);
            if(pos==-1) break;
			captured = re.capturedTexts();
            item->subId=captured[1].toFloat()/60;
            item->extra=captured[1].toFloat();
            if(item->title.isEmpty())
            {
                re.setPattern("<meta name=\"title\" content=\"(.*)\" />");
                pos=re.indexIn(replyStr);
                if(pos!=-1)
                {
                    item->title=re.capturedTexts()[1];
                }
            }
            downloadAllDanmu(item->strId,item->subId,danmuList);
            hasError=false;
        }while(false);
        if(hasError) errInfo=tr("Decode Failed");
    }
    catch(Network::NetworkError &error)
    {
        errInfo=error.errorInfo;
    }
    emit downloadDone(errInfo,item);
    return errInfo;
}

QString YoukuProvider::downloadBySourceURL(const QString &url, QList<DanmuComment *> &danmuList)
{
    QStringList info(url.split(';',QString::SkipEmptyParts));
    QString id(info[0].mid(info[0].indexOf(':')+1));
    int l=info[1].mid(info[1].indexOf(':')+1).toInt();
    downloadAllDanmu(id,l,danmuList);
    return QString();
}

void YoukuProvider::downloadAllDanmu(const QString &id, int length, QList<DanmuComment *> &danmuList)
{
    QString baseUrl("http://service.danmu.youku.com/list");
    QList<QPair<QString,QString> > queryItems({
        {"mcount","1"},{"ct","1001"},{"iid",id}
    });
    QStringList urls;
    QList<QUrlQuery> querys;
    for (int i=0;i<=length;++i)
    {
        urls<<baseUrl;
        queryItems.append(QPair<QString,QString>("mat",QString::number(i)));
        QUrlQuery query;
        query.setQueryItems(queryItems);
        queryItems.removeLast();
        querys<<query;
    }
    QList<QPair<QString, QByteArray> > results(Network::httpGetBatch(urls,querys));
    for(auto &result:results)
    {
        if(!result.first.isEmpty()) continue;
        QJsonObject obj(Network::toJson(result.second).object());
        QJsonArray danmuArray(obj.value("result").toArray());
        for(auto iter=danmuArray.begin();iter!=danmuArray.end();++iter)
        {
            QJsonObject dmObj=(*iter).toObject();
            QJsonValue content=dmObj.value("content");
            if(!content.isString()) continue;
            QJsonValue date=dmObj.value("createtime");
            if(!date.isDouble()) continue;
            QJsonValue uid=dmObj.value("uid");
            if(!uid.isDouble()) continue;
            QJsonValue playat=dmObj.value("playat");
            if(!playat.isDouble()) continue;

            int posVal=3, colorVal=0xffffff;
            try
            {
                QJsonObject pobj(Network::toJson(dmObj.value("propertis").toString()).object());
                QJsonValue pos = pobj.value("pos");
                if (pos.isDouble()) posVal = pos.toInt();
                else if (pos.isString()) posVal = pos.toString().toInt();
                else
                    posVal = 3;
                QJsonValue color = pobj.value("color");
                if (color.isDouble()) colorVal = color.toInt();
                else if (color.isString()) colorVal = color.toString().toInt();
                else
                    colorVal = 0xffffff;
            }
            catch (const Network::NetworkError &)
            {

            }

            DanmuComment *danmu=new DanmuComment();
            danmu->text=content.toString();
            danmu->time =playat.toInt();
            danmu->originTime=danmu->time;
            danmu->color= colorVal;
            danmu->fontSizeLevel=DanmuComment::Normal;
            if(posVal==3) danmu->setType(1);
            else if(posVal==4) danmu->setType(5);
            else if(posVal==6) danmu->setType(4);
            else danmu->setType(1);

            danmu->sender="[Youku]"+QString::number((long long)uid.toDouble());
            danmu->date=static_cast<long long>(date.toDouble()/1000);
            danmuList.append(danmu);
        }
    }
}

void YoukuProvider::handleSearchReply(QString &reply, DanmuAccessResult *result)
{
    HTMLParserSax parser(reply);
    QRegExp anchorReg("<div type=\"(\\d+)\" data-name=\"m_pos\">");
    int pos=reply.indexOf(anchorReg, 0);

    while(pos != -1)
    {
        parser.seekTo(pos);
        QString type = anchorReg.capturedTexts()[1];
        int nextPos = reply.indexOf(anchorReg,pos+1);
        int marginPos = nextPos;
        if(marginPos == -1)
        {
            marginPos = reply.indexOf("<div style=\"margin-bottom", pos+1);
            if(marginPos == -1) marginPos = reply.length();
        }
        if(type != "1005" && type != "1027")
        {
            pos = nextPos;
            continue;
        }
        while(!parser.currentNodeProperty("class").startsWith("title_")) parser.readNext();
        if(type == "1027") parser.readNext();
        DanmuSourceItem item;
        item.strId = parser.currentNodeProperty("href");
        if(item.strId.startsWith("//")) item.strId.push_front("http:");
        parser.readNext(); parser.readNext();
        item.title = parser.readContentUntil("a", false);
        QRegExp lre("<.*>");
        lre.setMinimal(true);
        item.title.replace(lre, "");
        result->list.append(item);


        if(type == "1027")
        {
            while(parser.curPos() < marginPos)
            {
                if(parser.currentNodeProperty("class").startsWith("box-item"))
                {
                    if(!parser.currentNodeProperty("title").isEmpty())
                    {
                        DanmuSourceItem epItem;
                        epItem.title = QString("%1 %2").arg(item.title, parser.currentNodeProperty("title"));
                        while(parser.currentNode()!="a") parser.readNext();
                        epItem.strId = parser.currentNodeProperty("href");
                        if(epItem.strId.startsWith("//")) epItem.strId.push_front("http:");
                        result->list.append(epItem);
                    }
                }
                parser.readNext();
            }
        }
        pos=nextPos;
    }
    result->error = false;
}
