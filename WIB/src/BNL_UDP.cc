#include <BNL_UDP.hh>
#include <BNL_UDP_Exception.hh>
#include <sys/socket.h>
#include <string.h> //memset, strerro
#include <errno.h>
#include <string>
#include <fcntl.h> //fcntl
#include <iostream>
#include <sstream>
#include <iomanip>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm> //STD::COUNT
#include "trace.h"

#define WIB_PACKET_KEY 0xDEADBEEF
#define WIB_REQUEST_PACKET_TRAILER 0xFFFF

#define WIB_RPLY_PACKET_SIZE 12

#define TIMEOUT_SECONDS 2
#define TIMEOUT_MICROSECONDS 0
struct WIB_packet_t
{
  uint32_t key;
  uint32_t reg_addr : 16;
  uint32_t data_MSW : 16;
  uint32_t data_LSW : 16;
  uint32_t trailer  : 16;
};

gige_reg_t* BNL_UDP::gige_reg_init(const char *IP_address, char *iface)
{
  const std::string identification = "BNL_UDP::gige_reg_init";
  TLOG_INFO(identification)<<"IP address is " << IP_address << TLOG_ENDL;
  int rc = 0;

  gige_reg_t *ret;
  ret = (gige_reg_t*)malloc(sizeof(gige_reg_t));
  if (ret == NULL) 
  { 
    printf("*** Fatal malloc error %s ***\n", strerror(errno));
    return NULL;
  }
  sprintf(ret->client_ip_addr, "%s", IP_address);
   
  // Recv socket
  ret->sock_recv = socket(AF_INET, SOCK_DGRAM, 0);

  int  flags;
  if((flags = fcntl(ret->sock_recv,F_GETFL,0)) < 0)
  {
    printf("Error fcntl %s\n", strerror(errno));
  }

  if(fcntl(ret->sock_recv,F_SETFL,flags ) < 0)
  {
    printf("Error on fcntl_set %s\n", strerror(errno));
  }

  if (ret->sock_recv == -1) 
  {
    printf("Error defining ret->sock_recv\n");
    perror(__func__);
    return NULL;
  }
    
  // Recv Port Setup
  bzero(&ret->si_recv, sizeof(ret->si_recv));
  ret->si_recv.sin_family = AF_INET;
  ret->si_recv.sin_addr.s_addr = htonl(INADDR_ANY);
  ret->si_recv.sin_port = htons(replyPort);
 
  // Bind to Register RX Port
  rc = bind(ret->sock_recv, (struct sockaddr *)&ret->si_recv,
	    sizeof(ret->si_recv));
  if (rc < 0) 
  {
    printf("Binding failed! %s\n",strerror(errno));
    perror(__func__);
    return NULL;
  }
   
  // Setup client READ TX
  ret->sock_read = socket(AF_INET, SOCK_DGRAM, 0);
  if (ret->sock_read == -1) 
  {
    printf("Failed sock_read socket call %s!\n", strerror(errno));
    perror(__func__);
    return NULL;
  }
    
  bzero(&ret->si_read, sizeof(ret->si_read));
  ret->si_read.sin_family = AF_INET;
  ret->si_read.sin_addr.s_addr = htonl(INADDR_ANY);
  ret->si_read.sin_port = htons(readPort);
 
  if (inet_aton(ret->client_ip_addr , &ret->si_read.sin_addr) == 0) 
  {
    printf("inet_aton() failed %s\n",strerror(errno));
  }

  // Bind to Register READ TX Port
  rc = connect(ret->sock_read, (struct sockaddr *)&ret->si_read,
	       sizeof(ret->si_read));
  if (rc < 0) 
  {
    printf("connect to read port failed %s\n", strerror(errno));
    perror(__func__);
    return NULL;
  }

  ret->si_lenr = sizeof(ret->si_read);
 
  // Setup client WRITE TX

  ret->sock_write =  socket(AF_INET, SOCK_DGRAM, 0);
  if (ret->sock_write == -1) 
  {
    printf("falied sock_write socket call %s\n", strerror(errno));
    perror(__func__);
    return NULL;
  }
 
  bzero(&ret->si_write, sizeof(ret->si_write));
  ret->si_write.sin_family = AF_INET;
  ret->si_write.sin_port = htons(writePort);
  ret->si_write.sin_addr.s_addr = htonl(INADDR_ANY);

  if (inet_aton(ret->client_ip_addr , &ret->si_write.sin_addr) == 0) 
  {
    printf("inet_aton() failed %s\n", strerror(errno));
  }

  // Bind to  Write TX Port
  rc = connect(ret->sock_write ,(struct sockaddr *)&ret->si_write,
		 sizeof(ret->si_write));
  if (rc < 0) 
  {
    perror(__func__);
    return NULL;
  }
  ret->si_lenw = sizeof(ret->si_write);
    
  return ret;
}


bool BNL_UDP::gige_reg_close(gige_reg_t* gige_reg){
  const std::string identification = "BNL_UDP::gige_reg_close";

  if(!gige_reg) {
    TLOG_ERROR(identification)<< "gige_reg is a nullptr";

    return false;
  }

  int error_count=0;

  if (close(gige_reg->sock_recv) == -1) {
    printf("Failed to close recv socket call %s!\n", strerror(errno));
    perror(__func__);
    error_count++;
  }

  if (close(gige_reg->sock_read) == -1) {
    printf("Failed to close read socket call %s!\n", strerror(errno));
    perror(__func__);
    error_count++;
  }

  if (close(gige_reg->sock_write) == -1) {
    printf("Failed to close write socket call %s!\n", strerror(errno));
    perror(__func__);
    error_count++;
  }

  if (error_count > 0) return false;

  free(gige_reg);

  return true;
}


static std::string dump_packet(uint8_t * data, size_t size)
{
  //  printf("Err: %p %zu\n",data,size);

  std::stringstream ss;
  for(size_t iWord = 0; iWord < size; iWord++)
  {
    ss << "0x" << std::hex << std::setfill('0') << std::setw(4) << iWord<<std::dec;
    ss << ": 0x" << std::hex << std::setfill('0') << std::setw(2) << int(data[iWord]) <<std::dec;
    iWord++;
    if(iWord < size)
    {
      ss << std::hex << std::setfill('0') << std::setw(2) << int(data[iWord]) << std::dec;
    }
    ss << std::dec << std::endl;
  }

  //  printf("%s",ss.str().c_str());
  return ss.str();
}

void BNL_UDP::FlushSocket(int sock)
{
  //turn on non-blocking
  fcntl(sock, F_SETFL, fcntl(sock, F_GETFL)| O_NONBLOCK);
  int ret;
  do
  {
    ret = recv(sock,buffer,buffer_size,0);      
  }
  while(ret != -1);

  // turn back on blocking
  fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & (~O_NONBLOCK));
}
  
void BNL_UDP::Clear()
{
  writeAck  = false;
  connected = false;
}

//static void printaddress(struct sockaddr_in const * addr){
//  printf("%u: %u.%u.%u.%u : %u\n",
//	 addr->sin_family,
//	 (addr->sin_addr.s_addr >> 0)&0xFF,
//	 (addr->sin_addr.s_addr >> 8)&0xFF,
//	 (addr->sin_addr.s_addr >> 16)&0xFF,
//	 (addr->sin_addr.s_addr >> 24)&0xFF,
//	 ntohs(addr->sin_port));
//}

void BNL_UDP::Setup(std::string const & address,uint16_t port_offset)
{
  const std::string identification = "BNL_UDP::Setup";
  //Reset the network structures
  Clear();

  //Allocate the recv buffer
  ResizeBuffer();

  //Check port_offset range
  if(port_offset > 128)
  {
    WIBException::BNL_UDP_PORT_OUT_OF_RANGE e;
    throw e;    
  }

  isFEMB = (port_offset != 0 );

  // To run multiple WIB BoardReaders on one server, they all need unique 
  //   response addresses
  int32_t b3,b2,b1,b0;
  sscanf(address.c_str(), "%d.%d.%d.%d", &b3,&b2,&b1,&b0);
  TLOG_INFO(identification) << "Decoded IP Address: " << b3 << "." << b2 << "." << b1 << "." << b0 <<TLOG_ENDL;

  if (isFEMB )
  {
    switch ( port_offset )
    {
      case 1:
	replyPort = FEMB1_REPLY_BASE + b0;
	writePort = FEMB1_RW_BASE;
	readPort  = FEMB1_RW_BASE + 1;
      break;

      case 2:
	replyPort = FEMB2_REPLY_BASE + b0;
	writePort = FEMB2_RW_BASE;
	readPort  = FEMB2_RW_BASE + 1;
      break;

      case 3:
	replyPort = FEMB3_REPLY_BASE + b0;
	writePort = FEMB3_RW_BASE;
	readPort  = FEMB3_RW_BASE + 1;
      break;

      case 4:
	replyPort = FEMB4_REPLY_BASE + b0;
	writePort = FEMB4_RW_BASE;
	readPort  = FEMB4_RW_BASE + 1;
      break;
    }
    TLOG_INFO(identification) << "FEMB " << port_offset << TLOG_ENDL;
  }
  else if ( isMBB )
  {
    TLOG_INFO(identification) << "MBB " << TLOG_ENDL;
    // MBB still uses old scheme
    writePort = MBB_WR_BASE_PORT;
    readPort  = MBB_RD_BASE_PORT;
    replyPort = MBB_RPLY_BASE_PORT;
  }
  else // Only WIBs left
  {
    TLOG_INFO(identification) << "WIB " << TLOG_ENDL;
    //WIB: Set the ports for this device (FEMBs are iFEMB*0x10 above the base)
    writePort = WIB_WR_BASE_PORT   + port_offset;
    readPort  = WIB_RD_BASE_PORT   + port_offset;

    // Add least byte of IP address to get unique reply port
    replyPort = WIB_RPLY_BASE_PORT + port_offset + b0;
  }
  TLOG_INFO(identification)<< "WritePort:    " << std::hex << writePort << TLOG_ENDL;
  TLOG_INFO(identification) << "ReadPort:     " << readPort << TLOG_ENDL;
  TLOG_INFO(identification) << "ResponsePort: " << replyPort << std::dec << TLOG_ENDL;

  remoteAddress = address;

  //Get the sockaddr for the address
  struct addrinfo * res;
  if(getaddrinfo(address.c_str(),NULL,NULL,&res))
  {
    //Check if we have just one "." character and is less than 5 characters
    if(address.size() <= 5 && 1 == std::count(address.begin(),address.end(),'.'))
    {
      std::string strCrate = address.substr(0,address.find('.'));
      std::string strSlot  = address.substr(address.find('.')+1);
      if(strCrate.size() != 0 && strSlot.size() != 0)
      {
	uint8_t crate = strtoul(strCrate.c_str(),NULL,0);
	uint8_t slot  = strtoul(strSlot.c_str(), NULL,0);
	if( (((crate > 0) && (crate < 7)) || 0xF == crate) &&
	    (((slot > 0) && (slot < 7)) || 0xF == slot))
	{
	  remoteAddress = std::string("192.168.");
	  //Add the crate part of the address (200 + crate number)
	  if(crate == 0xF)
	  {
	    remoteAddress += "200.";
	  }
	  else
	  {
	    //generate the crate number which is 200 + crate number
	    remoteAddress += "20";
	    remoteAddress += ('0' + crate);
	    remoteAddress += '.';
	  }
	  if(slot == 0xF)
	  {
	    remoteAddress += "50";
	  }
	  else
	  {
	    //crate last IP octet that is slot number
	    remoteAddress += ('0' + slot);
	  }
	}
      }
    }

    //try a second time assuming this is a crate.slot address, fail if this still doesn't work
    if(getaddrinfo(address.c_str(),NULL,NULL,&res))
    {
      WIBException::BAD_REMOTE_IP e;
      e.Append("Addr: ");
      e.Append(address.c_str());
      e.Append(" could not be resolved.\n");
      throw e;
    }
  }
  freeaddrinfo(res);

  reg = gige_reg_init(address.c_str(), NULL );

  //Allocate the receive buffer to default size
  ResizeBuffer();
}

void BNL_UDP::Disconnect()
{
  const std::string identification = "BNL_UDP::Disconnect";

  if (!gige_reg_close(reg)){
    TLOG_ERROR(identification)<<"BNLUDP Failed closing connections.";
    return;
  }

  reg=nullptr;

  TLOG_INFO(identification)<<"BNLUDP Closed connections.";
}

void BNL_UDP::WriteWithRetry(uint16_t address, uint32_t value, uint8_t retry_count)
{
  const std::string identification = "WIB:WIB";
  TLOG_INFO(identification)<<"BNLUDP WriteWithRetry "<<address<<" "<<value<<"  "<<retry_count<< TLOG_ENDL;

  while(retry_count > 1)
  {
    try
    {
      //Do the write
      TLOG_INFO(identification) <<"Trying..." << TLOG_ENDL;
      Write(address,value);
      TLOG_INFO(identification)<<"Sleeping..." << TLOG_ENDL;
      usleep(10);

      //if everything goes well, return
      TLOG_INFO(identification)<<"Return" << TLOG_ENDL;
      return;
    }
    catch(WIBException::BAD_REPLY &e)
    {
      //eat the exception
    }
    total_retry_count++;
    retry_count--;
    usleep(10);
    TLOG_INFO(identification)<<total_retry_count<<TLOG_ENDL;
  }

  //Last chance we don't catch the exception and let it fall down the stack
  //Do the write
  TLOG_INFO(identification)<<"Last chance" << TLOG_ENDL;
  Write(address,value);
  usleep(10);
}

void BNL_UDP::Write(uint16_t address, uint32_t value)
{
  //Flush this socket
  FlushSocket(reg->sock_recv);

  //Build the packet to send
  WIB_packet_t packet;
  packet.key = htonl(WIB_PACKET_KEY);
  packet.reg_addr = htons(address);
  packet.data_MSW = htons(uint16_t((value >> 16) & 0xFFFF));
  packet.data_LSW = htons(uint16_t((value >>  0) & 0xFFFF));
  packet.trailer  = htons(WIB_REQUEST_PACKET_TRAILER);

  //send the packet
  ssize_t send_size = sizeof(packet);
  ssize_t sent_size = sendto( reg->sock_write, &packet,send_size,0,
			      (struct sockaddr *) &reg->si_write, sizeof(reg->si_write));
  if( send_size != sent_size )
  {
    //bad send
    WIBException::SEND_FAILED e;
    if(sent_size == -1)
    {
      e.Append("BNL_UDP::Write(uint16_t,uint32_t)\n");
      e.Append("Errnum: ");
      e.Append(strerror(errno));
    } 
    throw e;
  }

  //If configured, capture confirmation packet
  // - check if FEMB, and skip if so
  if(writeAck && !isFEMB )
  {
    size_t reply_size = 0;
    struct sockaddr_in si_other;
    socklen_t len = sizeof(struct sockaddr_in);
    reply_size = recvfrom(reg->sock_recv, buffer, buffer_size, 0, 
			  (struct sockaddr *)&si_other, &len);
    if(-1 == (int)reply_size)
    {
      WIBException::BAD_REPLY e;
      std::stringstream ss;
      e.Append("BNL_UDP::Write(uint16_t,uint32_t)\n");
      ss << "Errnum(" << errno << "): " << strerror(errno) << "\n";
      e.Append(ss.str().c_str());
      e.Append(dump_packet((uint8_t*) &packet,send_size).c_str());
      throw e;
    }
    else if ( reply_size < WIB_RPLY_PACKET_SIZE)
    {
      WIBException::BAD_REPLY e;
      std::stringstream ss;
      ss << "Bad Size: " << reply_size << "\n";
      e.Append("BNL_UDP::Write(uint16_t,uint32_t)\n");
      e.Append(ss.str().c_str());
      e.Append(dump_packet(buffer,reply_size).c_str());
      throw e;
    }

    uint16_t reply_address =  uint16_t(buffer[0] << 8 | buffer[1]);
    if( reply_address != address)
    {
      WIBException::BAD_REPLY e;
      std::stringstream ss;
      ss << "Bad address: " << uint32_t(address) << " != " << uint32_t(reply_address) << "\n";
      e.Append("BNL_UDP::Write(uint16_t,uint32_t)\n");
      e.Append(ss.str().c_str());
      e.Append(dump_packet(buffer,reply_size).c_str());
      throw e;    
    }    
  }
}
void BNL_UDP::Write(uint16_t address, std::vector<uint32_t> const & values)
{
  Write(address,values.data(),values.size());
}

void BNL_UDP::Write(uint16_t address, uint32_t const * values, size_t word_count)
{
  for(size_t iWrite = 0; iWrite < word_count;iWrite++)
  {
    WriteWithRetry(address,values[iWrite]);
    address++;
  }
}

uint32_t BNL_UDP::ReadWithRetry(uint16_t address,uint8_t retry_count)
{
  uint32_t val;
  while(retry_count > 1)
  {
    try
    {
      //Do the write
      val = Read(address);
      usleep(10);
      //if everything goes well, return
      return val;
    }
    catch(WIBException::BAD_REPLY &e)
    {
      //eat the exception
    }
    usleep(10);
    total_retry_count++;
    retry_count--;
  }

  //Last chance we don't catch the exception and let it fall down the stack
  val = Read(address);  
  usleep(10);
  return val;
}

uint32_t BNL_UDP::Read(uint16_t address)
{
  //Flush the socket
  FlushSocket(reg->sock_recv);

  //build the send packet
  WIB_packet_t packet;
  packet.key = htonl(WIB_PACKET_KEY);
  packet.reg_addr = htons(address);
  packet.data_MSW = packet.data_LSW = 0;
  packet.trailer = htons(WIB_REQUEST_PACKET_TRAILER);

  //send the packet
  ssize_t send_size = sizeof(packet);
  ssize_t sent_size = 0;

  sent_size = sendto( reg->sock_read, &packet , send_size, 0 ,
		      (struct sockaddr *) &reg->si_read, sizeof(reg->si_read));
  if( sent_size != send_size )
  {
    //bad send
    WIBException::SEND_FAILED e;
    printf("Read sendto %d %s\n", errno, strerror(errno));
    e.Append("BNL_UDP::Read(uint16_t) send\n");
    e.Append("Errnum: ");
    e.Append(strerror(errno));
    throw e;
  }

  //Get the reply packet with the register data in it.   
  struct sockaddr_in si_other;
  socklen_t len = sizeof(struct sockaddr_in);
  size_t reply_size = 0;

  reply_size = recvfrom( reg->sock_recv, buffer, buffer_size, 0, 
			 (struct sockaddr *)&si_other, &len);
  if((int)reply_size == -1)
  {
    WIBException::BAD_REPLY e;
    std::stringstream ss;
    e.Append("BNL_UDP::Read(uint16_t) receive 1\n");
    ss << "Errnum(" << errno << "): " << strerror(errno) << "\n";
    e.Append(ss.str().c_str());
    e.Append(dump_packet((uint8_t *)&packet,send_size).c_str());
    throw e;
  }
  else if( reply_size < WIB_RPLY_PACKET_SIZE)
  {
    WIBException::BAD_REPLY e;
    std::stringstream ss;
    ss << "Bad Size: " << reply_size << "\n";
    e.Append("BNL_UDP::Read(uint16_t) receive 2\n");
    e.Append(ss.str().c_str());
    e.Append(dump_packet(buffer,reply_size).c_str());
    throw e;
  }

  uint16_t reply_address =  uint16_t(buffer[0] << 8 | buffer[1]);
  if( reply_address != address)
  {
    WIBException::BAD_REPLY e;
    std::stringstream ss;
    ss << "Bad address: " << uint32_t(address) << " != " << uint32_t(reply_address) << "\n";
    e.Append("BNL_UDP::Read(uint16_t)\n");
    e.Append(ss.str().c_str());
    e.Append(dump_packet(buffer,reply_size).c_str());
    throw e;    
  }
  
  uint32_t ret = ( (uint32_t(buffer[2]) << 24) | 
		   (uint32_t(buffer[3]) << 16) | 
		   (uint32_t(buffer[4]) <<  8) | 
		   (uint32_t(buffer[5]) <<  0));

  return ret;
}


BNL_UDP::~BNL_UDP()
{
  Clear();
  Disconnect();

  if(buffer != NULL) delete [] buffer;
}


void BNL_UDP::ResizeBuffer(size_t size)
{
  //CHeck if the requested size is larger than the already allocated size
  if(buffer_size < size)
  {
    //We need to re-allocate
    if(buffer != NULL){
      delete [] buffer;
    }
    buffer = new uint8_t[size];
    buffer_size = size;
  }
  //  printf("after %p %zu %zd\n",buffer,buffer_size,buffer_size);
}


