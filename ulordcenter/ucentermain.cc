#ifdef MYSQL_ENABLE
#include "utils.h"
#include "ucenter.h"

#include "util.h"
#include "privsend.h"

using namespace std;
static boost::scoped_ptr<ECCVerifyHandle> global_VerifyHandle;

void InitChain()
{
    if(GetBoolArg("-testnet", false))
	{
        SelectParams(CBaseChainParams::TESTNET);
		printf("Info: select TEST net!\n");
	}
    else
	{
        SelectParams(CBaseChainParams::MAIN);
		printf("Info: select MAIN net!\n");
	}

    ECC_Start();
	global_VerifyHandle.reset(new ECCVerifyHandle());
}

int main(int argc, char const *argv[])
{
    /*init*/
    SetFilePath("ulordcenter.conf");
	LoadConfigFile(mapArgs, mapMultiArgs);
    InitChain();
    InitLog(argv);
    CKey ucenterPrivkey;
    CPubKey ucenterPubkey;
    std::string strUCenterPrivKey = GetArg("-privkey", "");
    if(!strUCenterPrivKey.empty()) {
        if(!privSendSigner.GetKeysFromSecret(strUCenterPrivKey, ucenterPrivkey, ucenterPubkey)) {
            printf("Invalid ulord center private key in the configuration!\n");
            return -1;
        }
    } else {
        printf("You must specify a Ulord Center privkey in the configuration.!\n");
        return -1;
    }

	MysqlConnectInfo * ptrDBInfo = new MysqlConnectInfo(GetArg("-dbhost", "127.0.0.1"),
														GetArg("-dbport", 3306),
														GetArg("-dbuser", "root"),
                                        				GetArg("-dbpwd", "123456"),
                                        				GetArg("-dbname", "mysql"));
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(GetArg("-tcpport", 5009));
    InetAddress serverAddr(port);
    UlordServer utcenter(&loop, GetArg("-idleseconds", 60), serverAddr, ucenterPrivkey, *ptrDBInfo);
    int nThread = GetArg("-thread", 2);
    if (nThread > 1)
        utcenter.setThreadNum(nThread);
    utcenter.start();
    loop.loop();
    
    return 0;
}

#endif // MYSQL_ENABLE