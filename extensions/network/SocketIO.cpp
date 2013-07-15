/****************************************************************************
 Copyright (c) 2010-2013 cocos2d-x.org
 Copyright (c) 2013 Chris Hannon
 
 http://www.cocos2d-x.org
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.

*based on the SocketIO library created by LearnBoost at http://socket.io
*using spec version 1 found at https://github.com/LearnBoost/socket.io-spec

 ****************************************************************************/

#include "SocketIO.h"
#include "cocos-ext.h"
#include "network/WebSocket.h"
#include <algorithm>

NS_CC_EXT_BEGIN

//class declarations

/**
 *  @brief The implementation of the socket.io connection
 *		   Clients/endpoints may share the same impl to accomplish multiplexing on the same websocket
 */
class SIOClientImpl : 
	public Object, 
	public WebSocket::Delegate
{
private: 
	int _port, _heartbeat, _timeout;
	std::string _host, _sid, _uri;
	bool _connected;

	WebSocket *_ws;

	Dictionary* _clients;

public:
	SIOClientImpl(const std::string& host, int port);
	virtual ~SIOClientImpl(void);

	static SIOClientImpl* create(const std::string& host, int port);
	
	virtual void onOpen(cocos2d::extension::WebSocket* ws);
    virtual void onMessage(cocos2d::extension::WebSocket* ws, const cocos2d::extension::WebSocket::Data& data);
    virtual void onClose(cocos2d::extension::WebSocket* ws);
    virtual void onError(cocos2d::extension::WebSocket* ws, const cocos2d::extension::WebSocket::ErrorCode& error);

	void connect();
	void disconnect();
	bool init();
	void handshake();
	void handshakeResponse(HttpClient *sender, HttpResponse *response);
	void openSocket();
	void heartbeat(float dt);

	SIOClient* getClient(const std::string& endpoint);
	void addClient(const std::string& endpoint, SIOClient* client);
	
	void connectToEndpoint(const std::string& endpoint);
	void disconnectFromEndpoint(const std::string& endpoint);

	void send(std::string endpoint, std::string s);
	void emit(std::string endpoint, std::string eventname, std::string args);


};
	

//method implementations

//begin SIOClientImpl methods
SIOClientImpl::SIOClientImpl(const std::string& host, int port) :
	_port(port),
	_host(host),
	_connected(false)
{
	_clients = Dictionary::create();
	_clients->retain();

	std::stringstream s;
	s << host << ":" << port;
	_uri = s.str();

	_ws = NULL;	

}

SIOClientImpl::~SIOClientImpl() {

	if(_connected) disconnect();

	CC_SAFE_DELETE(_clients);
	CC_SAFE_DELETE(_ws);

}

void SIOClientImpl::handshake() {
	CCLog("SIOClientImpl::handshake() called");

	std::stringstream pre;
	pre << "http://" << _uri << "/socket.io/1";

	HttpRequest* request = new HttpRequest();
	request->setUrl(pre.str().c_str());
    request->setRequestType(HttpRequest::kHttpGet);

    request->setResponseCallback(this, httpresponse_selector(SIOClientImpl::handshakeResponse));
    request->setTag("handshake");

	CCLog("SIOClientImpl::handshake() waiting");

    HttpClient::getInstance()->send(request);
	    
	request->release();
	
	return;
}

void SIOClientImpl::handshakeResponse(HttpClient *sender, HttpResponse *response) {

	CCLog("SIOClientImpl::handshakeResponse() called");

	if (0 != strlen(response->getHttpRequest()->getTag())) 
    {
        CCLog("%s completed", response->getHttpRequest()->getTag());
    }

	int statusCode = response->getResponseCode();
    char statusString[64] = {};
    sprintf(statusString, "HTTP Status Code: %d, tag = %s", statusCode, response->getHttpRequest()->getTag());
	CCLog("response code: %d", statusCode);

	if (!response->isSucceed()) 
    {
        CCLog("SIOClientImpl::handshake() failed");
        CCLog("error buffer: %s", response->getErrorBuffer());

		DictElement* el = NULL;

		CCDICT_FOREACH(_clients, el) {

			SIOClient* c = static_cast<SIOClient*>(el->getObject());
			
			c->getDelegate()->onError(c, response->getErrorBuffer());

		}

        return;
    }

	CCLog("SIOClientImpl::handshake() succeeded");

	std::vector<char> *buffer = response->getResponseData();
	std::stringstream s;
   
	for (unsigned int i = 0; i < buffer->size(); i++)
    {
		s << (*buffer)[i];
    }
    
	CCLog("SIOClientImpl::handshake() dump data: %s", s.str().c_str());

	std::string res = s.str();
	std::string sid;
	int pos = 0;
	int heartbeat = 0, timeout = 0;

	pos = res.find(":");
	if(pos >= 0) {
		sid = res.substr(0, pos);
		res.erase(0, pos+1);
	}

	pos = res.find(":");
    if(pos >= 0){
        heartbeat = atoi(res.substr(pos+1, res.size()).c_str());
    }

	pos = res.find(":");
    if(pos >= 0){
        timeout = atoi(res.substr(pos+1, res.size()).c_str());
    }

	_sid = sid;
	_heartbeat = heartbeat;
	_timeout = timeout;
	
	openSocket();

	return;

}

void SIOClientImpl::openSocket() {
	
	CCLog("SIOClientImpl::openSocket() called");

	std::stringstream s;
	s << _uri << "/socket.io/1/websocket/" << _sid;

	_ws = new WebSocket();
	if(!_ws->init(*this, s.str())) 
	{
		CC_SAFE_DELETE(_ws);
	}
	
	return;
}

bool SIOClientImpl::init() {

	CCLog("SIOClientImpl::init() successful");
	return true;

}

void SIOClientImpl::connect() {

	this->handshake();

}

void SIOClientImpl::disconnect() {

	if(_ws->getReadyState() == WebSocket::kStateOpen) {

		std::string s = "0::";

		_ws->send(s);

		CCLog("Disconnect sent");

		_ws->close();

	}

	Director::getInstance()->getScheduler()->unscheduleAllForTarget(this);

	_connected = false;

	SocketIO::instance()->removeSocket(_uri);
		
}

SIOClientImpl* SIOClientImpl::create(const std::string& host, int port) {

	SIOClientImpl *s = new SIOClientImpl(host, port);

	if(s && s->init()) {

		return s;

	}

	return NULL;

}

SIOClient* SIOClientImpl::getClient(const std::string& endpoint) { 
	
	return static_cast<SIOClient*>(_clients->objectForKey(endpoint));

}

void SIOClientImpl::addClient(const std::string& endpoint, SIOClient* client) { 
	
	_clients->setObject(client, endpoint); 

}

void SIOClientImpl::connectToEndpoint(const std::string& endpoint) { 
	
	std::string path = endpoint == "/" ? "" : endpoint;

	std::string s = "1::" + path;

	_ws->send(s);

}

void SIOClientImpl::disconnectFromEndpoint(const std::string& endpoint) { 
	
	_clients->removeObjectForKey(endpoint);

	if(_clients->count() == 0 || endpoint == "/") {
		
		CCLog("SIOClientImpl::disconnectFromEndpoint out of endpoints, checking for disconnect");
		
		if(_connected) this->disconnect();
		
	} else {

		std::string path = endpoint == "/" ? "" : endpoint;

		std::string s = "0::" + path;

		_ws->send(s);

	}
	
}

void SIOClientImpl::heartbeat(float dt) {
	
	std::string s = "2::";

	_ws->send(s);

	CCLog("Heartbeat sent");

}


void SIOClientImpl::send(std::string endpoint, std::string s) {
	std::stringstream pre;

	std::string path = endpoint == "/" ? "" : endpoint;
	
	pre << "3::" << path << ":" << s;

	std::string msg = pre.str();

	CCLog("sending message: %s", msg.c_str());

	_ws->send(msg);

}

void SIOClientImpl::emit(std::string endpoint, std::string eventname, std::string args) {
	
	std::stringstream pre;

	std::string path = endpoint == "/" ? "" : endpoint;
	
	pre << "5::" << path << ":{\"name\":\"" << eventname << "\",\"args\":" << args << "}";

	std::string msg = pre.str();

	CCLog("emitting event with data: %s", msg.c_str());

	_ws->send(msg);

}

void SIOClientImpl::onOpen(cocos2d::extension::WebSocket* ws) {

	_connected = true;

	SocketIO::instance()->addSocket(_uri, this);

	DictElement* e = NULL;

	CCDICT_FOREACH(_clients, e) {

		SIOClient *c = static_cast<SIOClient*>(e->getObject());

		c->onOpen();
		
	}

	Director::sharedDirector()->getScheduler()->scheduleSelector(schedule_selector(SIOClientImpl::heartbeat), this, (_heartbeat * .9), false);
	
	CCLog("SIOClientImpl::onOpen socket connected!");

}

void SIOClientImpl::onMessage(cocos2d::extension::WebSocket* ws, const cocos2d::extension::WebSocket::Data& data) {

	CCLog("SIOClientImpl::onMessage received: %s", data.bytes);

	int control = atoi(&data.bytes[0]);

	std::string payload, msgid, endpoint, s_data, eventname;
	payload = data.bytes;

	int pos, pos2;

	pos = payload.find(":");
	if(pos >=0 ) {
		payload.erase(0, pos+1);
	}

	pos = payload.find(":");
	if(pos > 0 ) {
		msgid = atoi(payload.substr(0, pos+1).c_str());	
	}
	payload.erase(0, pos+1);

	pos = payload.find(":");
	if(pos >= 0) {

		endpoint = payload.substr(0, pos);
		payload.erase(0, pos+1);

	} else {

		endpoint = payload;
	}

	if(endpoint == "") endpoint = "/";


	s_data = payload;
	SIOClient *c = NULL;
	c = getClient(endpoint);
	if(c == NULL) CCLog("SIOClientImpl::onMessage client lookup returned NULL");

	switch(control) {
		case 0: 
			CCLog("Received Disconnect Signal for Endpoint: %s\n", endpoint.c_str());
			if(c) c->receivedDisconnect();
			disconnectFromEndpoint(endpoint);
			break;
		case 1: 
			CCLog("Connected to endpoint: %s \n",endpoint.c_str());
			if(c) c->onConnect();
			break;
		case 2: 
			CCLog("Heartbeat received\n");
			break;
		case 3:
			CCLog("Message received: %s \n", s_data.c_str());
			if(c) c->getDelegate()->onMessage(c, s_data);
			break;
		case 4:
			CCLog("JSON Message Received: %s \n", s_data.c_str());
			if(c) c->getDelegate()->onMessage(c, s_data);
			break;
		case 5:
			CCLog("Event Received with data: %s \n", s_data.c_str());

			if(c) {
				eventname = "";
				pos = s_data.find(":");
				pos2 = s_data.find(",");
				if(pos2 > pos) {
					s_data = s_data.substr(pos+1, pos2-pos-1);
					std::remove_copy(s_data.begin(), s_data.end(),
						 std::back_inserter(eventname), '"');
				}

				c->fireEvent(eventname, payload);
			}
			
			break;
		case 6:
			CCLog("Message Ack\n");
			break;
		case 7:
			CCLog("Error\n");
			if(c) c->getDelegate()->onError(c, s_data);
			break;
		case 8:
			CCLog("Noop\n");
			break;
	}

	return;
}

void SIOClientImpl::onClose(cocos2d::extension::WebSocket* ws) {

	if(_clients->count() > 0) {

		DictElement *e;

		CCDICT_FOREACH(_clients, e) {

			SIOClient *c = static_cast<SIOClient *>(e->getObject());

			c->receivedDisconnect();

		}

	}

	this->release();

}

void SIOClientImpl::onError(cocos2d::extension::WebSocket* ws, const cocos2d::extension::WebSocket::ErrorCode& error) {


}

//begin SIOClient methods
SIOClient::SIOClient(const std::string& host, int port, const std::string& path, SIOClientImpl* impl, SocketIO::SIODelegate& delegate) 
	: _host(host)
	, _port(port)
	, _path(path)
	, _socket(impl)
	, _connected(false)
	, _delegate(&delegate)
{


}

SIOClient::~SIOClient(void) {
	
	if(_connected) {
		_socket->disconnectFromEndpoint(_path);
	}

}

void SIOClient::onOpen() {

	if(_path != "/") {

			_socket->connectToEndpoint(_path);

	}
	
}

void SIOClient::onConnect() {

	_connected = true;
	_delegate->onConnect(this);
	
}

void SIOClient::send(std::string s) {

	if(_connected) {
		_socket->send(_path, s);
	} else {
		_delegate->onError(this, "Client not yet connected");
	}

}

void SIOClient::emit(std::string eventname, std::string args) {

	if(_connected) {
		_socket->emit(_path, eventname, args);
	} else {
		_delegate->onError(this, "Client not yet connected");
	}

}

void SIOClient::disconnect() {

	_connected = false;

	_socket->disconnectFromEndpoint(_path);

	_delegate->onClose(this);
	
	this->release();
	
}

void SIOClient::receivedDisconnect() {

	_connected = false;

	_delegate->onClose(this);

	this->release();

}

void SIOClient::on(const std::string& eventName, SIOEvent e) {

	_eventRegistry[eventName] = e;

}

void SIOClient::fireEvent(const std::string& eventName, const std::string& data) {

	CCLog("SIOClient::fireEvent called with event name: %s and data: %s", eventName.c_str(), data.c_str());

	if(_eventRegistry[eventName]) {

		SIOEvent e = _eventRegistry[eventName];

		e(this, data);

		return;
	}

	CCLog("SIOClient::fireEvent no event with name %s found", eventName.c_str());

}

//begin SocketIO methods
SocketIO *SocketIO::_inst = NULL;

SocketIO::SocketIO() {

	_sockets = Dictionary::create();
	_sockets->retain();

}

SocketIO::~SocketIO(void) {
	CC_SAFE_DELETE(_sockets);
	delete _inst;	
}

SocketIO* SocketIO::instance() {

	if(!_inst)
		_inst = new SocketIO();
	
	return _inst;

}

SIOClient* SocketIO::connect(SocketIO::SIODelegate& delegate, const std::string& uri) {

	std::string host = uri;
	int port, pos;

	pos = host.find("//");
	if(pos >= 0) {
		host.erase(0, pos+2);
	}

	pos = host.find(":");
    if(pos >= 0){
        port = atoi(host.substr(pos+1, host.size()).c_str());
    }

	pos = host.find("/", 0);
    std::string path = "/";
    if(pos >= 0){
        path += host.substr(pos + 1, host.size());
    }

	pos = host.find(":");
    if(pos >= 0){
        host.erase(pos, host.size());
    }else if((pos = host.find("/"))>=0) {
    	host.erase(pos, host.size());
    }

	std::stringstream s;
	s << host << ":" << port;
	
	SIOClientImpl* socket = NULL;
	SIOClient *c = NULL;

	socket = SocketIO::instance()->getSocket(s.str());

	if(socket == NULL) {
		//create a new socket, new client, connect
		socket = SIOClientImpl::create(host, port);

		c = new SIOClient(host, port, path, socket, delegate);
	
		socket->addClient(path, c);

		socket->connect();

		

	} else {
		//check if already connected to endpoint, handle
		c = socket->getClient(path);

		if(c == NULL) {

			c = new SIOClient(host, port, path, socket, delegate);
	
			socket->addClient(path, c);

			socket->connectToEndpoint(path);

		}

	}		
	
	return c;

}

SIOClientImpl* SocketIO::getSocket(const std::string& uri) { 
	
	return static_cast<SIOClientImpl*>(_sockets->objectForKey(uri)); 
		
}

void SocketIO::addSocket(const std::string& uri, SIOClientImpl* socket) { 
	_sockets->setObject(socket, uri); 
}

void SocketIO::removeSocket(const std::string& uri) { 
	_sockets->removeObjectForKey(uri);
}

NS_CC_EXT_END