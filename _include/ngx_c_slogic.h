//
// Created by dietrich on 8/9/21.
//

#ifndef LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SLOGIC_H
#define LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SLOGIC_H

#include <sys/socket.h>
#include "ngx_c_socket.h"

class CLogicSocket : public CSocket {
public:
    CLogicSocket();
    virtual ~CLogicSocket();
    virtual bool Initialize();

public:
    bool _HandleRegister(std::shared_ptr <ngx_connection_poll> pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody,
                         unsigned short iBodyLength);

    bool _HandleLogIn(std::shared_ptr <ngx_connection_poll> pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody,
                      unsigned short iBodyLength);
};
#endif //LINUX_CPP_COMM_ARCHITECTURE_NGX_C_SLOGIC_H
