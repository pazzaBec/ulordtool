#ifdef MYSQL_ENABLE
#include <string>

#include "ulordcenter.h"
#include "ulord.h"

using namespace std;

std::string ParseHex2String(const char* psz)
{
    std::vector<unsigned char> vch = ParseHex(psz);
    std::string res;
    res.insert(res.begin(), vch.begin(), vch.end());
    return res;
}
std::string ParseHex2String(const std::string str)
{
    return ParseHex2String(str.c_str());
}

CUCenter::CUCenter(EventLoop* loop) :
idleSeconds_(GetArg("-idleseconds", 60)),
server_(loop, InetAddress(static_cast<uint16_t>(GetArg("-tcpport", 5009))), "UCenterServer"),
codec_(boost::bind(&CUCenter::onStringMessage, this, _1, _2, _3)),
licversion_(GetArg("-licversion",1)),
db_()
{
    server_.setConnectionCallback(boost::bind(&CUCenter::onConnection, this, _1));
    server_.setMessageCallback(boost::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
    loop->runEvery(1.0, boost::bind(&CUCenter::onTimer, this));
}

bool CUCenter::InitUCenterKey()
{
    char cKeyVersion[20];
    int keyVersion = 1;
    std::string strKey;
    
    while(true)
    {
        memset(cKeyVersion, 0, sizeof(cKeyVersion));
        sprintf(cKeyVersion, "-privkey%d", keyVersion);
        strKey = GetArg(std::string(cKeyVersion), "");
        if(strKey.empty()) {
            break;
        }

        try {
            CKeyExtension privkey(1, strKey);
            mapUCenterkey_.insert(pair_int_key_t(keyVersion, privkey.GetKey()));
            
            LOG(INFO) << "Load ucenter key <" << keyVersion << ":" << strKey << ">";
            keyVersion++;
        } catch(int) {
            printf("Invalid ulord center private key in the configuration! %s\n", strKey.c_str());
            return false;
        }
    }
    if(mapUCenterkey_.size() == 0) {
        printf("You must specify a Ulord Center privkey in the configuration.! example privkey1=123qwe\n");
        return false;
    }

    /*init masternode list*/
    mapMNodeList_.clear();
    CUlordDb::map_col_val_t mapSelect;
    mapSelect.insert(make_pair("status", to_string(1)));
    vector<CMNode> vecRet;
    if(db_.SelectMNode(mapSelect, vecRet)) {
        for(auto mn:vecRet) mapMNodeList_.insert(make_pair(CMNCoin(mn._txid, mn._voutid), mn));
    }
    printf("Load %zu masternodes from DB\n", mapMNodeList_.size());
    return true;
}

void CUCenter::onConnection(const TcpConnectionPtr & conn)
{
    LOG(INFO) << conn->localAddress().toIpPort() << " -> "
                << conn->peerAddress().toIpPort() << " is "
                << (conn->connected() ? "UP" : "DOWN");
    if (conn->connected()) { 
        Node node;
        node.lastReceiveTime = Timestamp::now();
        connectionList_.push_back(conn);
        node.position = --connectionList_.end();
        conn->setContext(node);
    } else { 
        assert(!conn->getContext().empty());
        const Node& node = boost::any_cast<const Node&>(conn->getContext());
        connectionList_.erase(node.position);
    }
    //dumpConnectionList();
}

void CUCenter::onStringMessage(const TcpConnectionPtr & tcpcli, const std::string & message, Timestamp time)
{
    uint32_t expectlen = GetArg("-requestlen",137);
    uint32_t expectlen_noboost = GetArg("-requestlennew",85);
    LOG(INFO) << "receive msg  "<< message.size() << " ,expect length is " << expectlen << " or " << expectlen_noboost;
    if(message.size() != expectlen) {
        if(message.size() == expectlen_noboost) {
            if(-1 == HandlerMsg(tcpcli, message))
                LOG(ERROR) << "receive message (" << HexStr(message.c_str(), (message.c_str()+expectlen_noboost)) << ") serialize exception!";
        }
        return;
    }
    
    //ParseQuest
    std::ostringstream os;
    boost::archive::binary_oarchive oa(os);
    std::string strinfo;
    std::vector<CMstNodeData> vecnode;
    mstnodequest mstquest;
    try {
        std::istringstream is(message);  
        boost::archive::binary_iarchive ia(is);  
        ia >> mstquest;//从一个保存序列化数据的string里面反序列化，从而得到原来的对象。
    }
    catch (const std::exception& ex) {
        if(!UnSerializeBoost(message, mstquest)) {
            LOG(ERROR) << "receive message (" << HexStr(message.c_str(), (message.c_str()+expectlen)) << ") serialize exception:" << ex.what();
            return;
        }
    }
    if(mstquest._questtype == MST_QUEST_ONE) {
        CMstNodeData msnode;
        if(!SelectMNData(mstquest._txid, mstquest._voutid, msnode))
            LOG(INFO) << "No valid info for masternode <" << mstquest._txid << ":" << mstquest._voutid << ">";
        else
            vecnode.push_back(msnode);
        if(vecnode.size()==0) {
            CMstNodeData node(0,mstquest._txid, mstquest._voutid);
            node._status = 0;
            vecnode.push_back(node);
        }
        mstnoderes  mstres(mstquest._msgversion);
        mstres._num = vecnode.size();
        mstres._nodetype = MST_QUEST_ONE;
        strinfo = Strings::Format("Send msg: %d masternodes\n", mstres._num);
        
        oa << mstres;
        for(auto & node : vecnode)
        {
            node._privkey = "secret";
            strinfo += Strings::Format("\t<%s:%d-%s> %s - %ld\n", node._txid.c_str(), node._voutid, HexStr(node._pubkey).c_str(), node._licence.c_str(), node._licperiod);
            oa << node;
        }
    } else if (mstquest._questtype == MST_QUEST_KEY) {
        mstnoderes  mstres(mstquest._msgversion);
        mstres._num= mapUCenterkey_.size();
        mstres._nodetype = MST_QUEST_KEY;
        strinfo = Strings::Format("Send msg: %d ucenter keys\n", mstres._num);

        oa << mstres;
        for(map_int_key_cit it = mapUCenterkey_.begin(); it != mapUCenterkey_.end(); it++)
        {
            CcenterKeyData keyPair(it->first, HexStr(it->second.GetPubKey()));
            strinfo += Strings::Format("\t<%d:%s>\n", it->first, HexStr(it->second.GetPubKey()).c_str());
            oa << keyPair;
        }
    } else {
        LOG(INFO) << "Unknown Msg";
        return;
    }
    LOG(INFO) << strinfo;
    std::string content = os.str();
    muduo::StringPiece sendmessage(content);
    codec_.send(tcpcli, sendmessage);
    return;
}

bool CUCenter::UnSerializeBoost(const std::string msg, mstnodequest& mq)
{
    std::string head = msg.substr(0,90);
    int version = Hex2Int(msg.substr(90,8));
    int64_t timestamp = Hex2Int64(msg.substr(98,16));
    int type = Hex2Int(msg.substr(114,8));
    int64_t txidlen = Hex2Int64(msg.substr(122,16));
    std::string txid = msg.substr(138,128);
    int voutid = Hex2Int(msg.substr(266,8));

    if(ParseHex2String(head).find("serialization::archive") != std::string::npos) return false;
    if(version != 111) return false;
    if(type != 1) return false;
    if(txidlen != 64) return false;

    mq._msgversion = version;
    mq._questtype = type;
    mq._timeStamps = timestamp;
    mq._txid = ParseHex2String(txid);
    mq._voutid = (unsigned int)voutid;

    LOG(WARNING) << "receive message request masternode " << mq._txid << "-" << mq._voutid << "by message " << HexStr(msg.c_str(), (msg.c_str()+msg.size()));
    return true;
}

int CUCenter::HandlerMsg(const TcpConnectionPtr & tcpcli, const std::string & message)
{
    std::string strinfo;
    std::vector<CMstNodeData> vecnode;
    mstnodequest mstquest;
    CDataStream oa(SER_NETWORK, PROTOCOL_VERSION);
    try {
        vector<char> vRcv;
        vRcv.insert(vRcv.end(), message.begin(), message.end());
        CDataStream rcv(vRcv, SER_NETWORK, PROTOCOL_VERSION);
        rcv >> mstquest;
    } catch (const std::exception& ex) {
        return -1;
    }
    if(mstquest._questtype == MST_QUEST_ONE) {
        CMstNodeData msnode;
        if(!SelectMNData(mstquest._txid, mstquest._voutid, msnode))
            LOG(INFO) << "No valid info for masternode <" << mstquest._txid << ":" << mstquest._voutid << ">";
        else
            vecnode.push_back(msnode);
        if(vecnode.size()==0) {
            CMstNodeData node(0,mstquest._txid, mstquest._voutid);
            node._status = 0;
            vecnode.push_back(node);
        }
        mstnoderes  mstres(mstquest._msgversion);
        mstres._num = vecnode.size();
        mstres._nodetype = MST_QUEST_ONE;
        strinfo = Strings::Format("Send msg: %d masternodes\n", mstres._num);
        
        oa << mstres;
        for(auto & node : vecnode)
        {
            node._privkey = "secret";
            strinfo += Strings::Format("\t<%s:%d-%s> %s - %ld\n", node._txid.c_str(), node._voutid, HexStr(node._pubkey).c_str(), node._licence.c_str(), node._licperiod);
            oa << node;
        }
    } else if (mstquest._questtype == MST_QUEST_KEY) {
        mstnoderes  mstres(mstquest._msgversion);
        mstres._num= mapUCenterkey_.size();
        mstres._nodetype = MST_QUEST_KEY;
        strinfo = Strings::Format("Send msg: %d ucenter keys\n", mstres._num);

        oa << mstres;
        for(map_int_key_cit it = mapUCenterkey_.begin(); it != mapUCenterkey_.end(); it++)
        {
            CcenterKeyData keyPair(it->first, HexStr(it->second.GetPubKey()));
            strinfo += Strings::Format("\t<%d:%s>\n", it->first, HexStr(it->second.GetPubKey()).c_str());
            oa << keyPair;
        }
    } else {
        LOG(INFO) << "Unknown Msg";
        return 0;
    }
    LOG(INFO) << strinfo;
    std::string content = oa.str();
    muduo::StringPiece sendmessage(content);
    codec_.send(tcpcli, sendmessage);
    return 1;
}

void CUCenter::onTimer()
{
    //dumpConnectionList();
    Timestamp now = Timestamp::now();
    for (WeakConnectionList::iterator it = connectionList_.begin(); it != connectionList_.end(); )
    {
        TcpConnectionPtr conn = it->lock();
        if (conn) {
            Node * n = boost::any_cast<Node>(conn->getMutableContext());
            double age = timeDifference(now, n->lastReceiveTime);
            if (age > idleSeconds_) {
                if (conn->connected()) {
                    conn->shutdown();
                    LOG(INFO) << "shutting down " << conn->name();
                    conn->forceCloseWithDelay(3.5);  // > round trip of the whole Internet.
                }
            } else if (age < 0) {
                LOG(WARNING) << "Time jump" << conn->name();
                n->lastReceiveTime = now;
            } else {
                break;
            }
            ++it;
        } else {
            LOG(WARNING) << "Expired conn";
            it = connectionList_.erase(it);
        }
    }
}

void CUCenter::dumpConnectionList() const
{
    for (WeakConnectionList::const_iterator it = connectionList_.begin(); it != connectionList_.end(); ++it)
    {
        TcpConnectionPtr conn = it->lock();
        if (conn) {
            const Node& n = boost::any_cast<const Node&>(conn->getContext());
            LOG(INFO) << "conn " << get_pointer(conn) << "\ttime " << n.lastReceiveTime.toString();
        } else {
            LOG(INFO) << "conn:expired";
        }
    }
}

bool CUCenter::SelectMNData(std::string txid, unsigned int voutid, CMstNodeData & mn)
{
    LOCK(cs_);
    const CMNCoin mnCoin(txid, voutid);
    int64_t tnow = GetTime();
    if(mapMNodeList_.count(mnCoin) != 0) {
        if(mapMNodeList_[mnCoin]._licperiod >= tnow + db_._needUpdatePeriod) {
            mn = *(mapMNodeList_[mnCoin].GetData());
            return true;
        } else {
            /*update mn list from db*/
            vector<std::string> vecFilter;
            vector<CMNode> vecRet;
            vecFilter.push_back("status=1");
            vecFilter.push_back(Strings::Format("validdate>%ld", tnow + db_._needUpdatePeriod));
            if(db_.SelectMNode(vecFilter, vecRet)) {
                for(auto mn:vecRet)
                {
                    CMNCoin tx(mn._txid, mn._voutid);
                    if(mapMNodeList_.count(tx) != 0) mapMNodeList_[tx] = mn;
                    else mapMNodeList_.insert(make_pair(tx, mn));
                }
            }
        }
    } else {
        /*update mn list from db*/
        vector<std::string> vecFilter;
        vector<CMNode> vecRet;
        vecFilter.push_back("status=1");
        vecFilter.push_back(Strings::Format("trade_txid='%s'", txid.c_str()));
        if(db_.SelectMNode(vecFilter, vecRet)) {
            for(auto mn:vecRet)
            {
                CMNCoin tx(mn._txid, mn._voutid);
                if(mapMNodeList_.count(tx) != 0) mapMNodeList_[tx] = mn;
                else mapMNodeList_.insert(make_pair(tx, mn));
            }
        }
    }

    if(mapMNodeList_.count(mnCoin) != 0) {
        mn = *(mapMNodeList_[mnCoin].GetData());
        return true;
    }
    return false;
}
#endif // MYSQL_ENABLE
