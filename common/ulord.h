#ifndef UTCENTER_ULORD_H
#define UTCENTER_ULORD_H
#include "base58.h"

class CKeyExtension : public CBase58Data
{
private:
    CPubKey pubkey_;
    std::string address_;
    bool SetKey(CKey key);
public:
    CKeyExtension(CKey key);
    CKeyExtension(bool bCompress);
    CKeyExtension(std::string strPrivkey);
	CKeyExtension(unsigned int nVersionBytes, std::string strPrivkey);
    CKey GetKey();
    bool IsValid();
    CPubKey GetPubKey() { return pubkey_; }
    std::string GetPubKeyString() { return HexStr(pubkey_); }
    std::string GetPubIdString();
    std::string GetAddress() { return address_; }
    bool SignCompact(std::string strMsg, std::vector<unsigned char>& vchSigRet);
    bool SignCompact(const uint256 &hash, std::vector<unsigned char>& vchSigRet);
    bool VerifyCompact(const std::vector<unsigned char>& vchSig, std::string strMsg, std::string & strErrRet);
    bool Match(std::string strPub);
	std::string Encode();
};

#endif // UTCENTER_ULORD_H
