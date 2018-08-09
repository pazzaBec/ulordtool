#ifdef MYSQL_ENABLE

#include "utils.h"
#include "dbwatcher.h"

#include "util.h"
#include "privsend.h"

using namespace std;
static boost::scoped_ptr<ECCVerifyHandle> global_VerifyHandle;

void InitChain()
{
    if(GetBoolArg("-testnet", false)) {
        SelectParams(CBaseChainParams::TESTNET);
		printf("Info: select TEST net!\n");
	} else {
        SelectParams(CBaseChainParams::MAIN);
		printf("Info: select MAIN net!\n");
        string err;
        if(!mnodecenter.InitCenter(err)) {
            printf("InitChain:%s\n", err.c_str());
        }
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

	MysqlConnectInfo * ptrDBInfo = new MysqlConnectInfo(GetArg("-dbhost", "127.0.0.1"),
														GetArg("-dbport", 3306),
														GetArg("-dbuser", "root"),
                                        				GetArg("-dbpwd", "123456"),
                                        				GetArg("-dbname", "mysql"));

    DBWatcher watcher = DBWatcher(*ptrDBInfo);
    if(!watcher.InitWatcherKey())
        return -1;

    if(argc > 1)
    {
        vector<CMstNodeData> vecnode;

        if("test" == string(argv[1]))
        {
            if(!watcher.IsDBOnline()) {
                printf("can't connect to db!\n");
                return -1;
            }
            watcher.SelectMNData(vecnode);
            watcher.UpdateDB(vecnode);
            return 0;
        }
        if("clear" == string(argv[1]))
        {
            watcher.SelectMNData(vecnode);
            for(auto & mn : vecnode) {
                watcher.ClearMNData(mn);
            }
            return 0;
        }
    }

    watcher.Runner();

    return 0;
}

#endif // MYSQL_ENABLE
