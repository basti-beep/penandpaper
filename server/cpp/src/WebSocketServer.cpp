#include "WebSocketServer.h"

#include <memory>
#include <thread>
#include <chrono>

#include "Logger.h"

WebSocketServer::WebSocketServer(OnMsgHandler_t on_msg,
                                 OnConnectHandler_t on_connect)
    : _on_msg(on_msg), _on_connect(on_connect) {
  std::thread t(&WebSocketServer::run, this);
  t.detach();
}

void WebSocketServer::run() {
  while (true) {
    try {
      _socket.clear_access_channels(websocketpp::log::alevel::all);
      _socket.init_asio();

      _socket.set_open_handler([this](websocketpp::connection_hdl conn_hdl) {
        try {
          _connections.push_back(conn_hdl);
          Response resp = _on_connect();
          handleResponse(resp, conn_hdl);
        } catch (const std::exception &e) {
          LOG_ERROR << "Error while handling a new client: " << e.what()
                    << LOG_END;
        } catch (...) {
          LOG_ERROR << "Unknown error while handling a new client." << LOG_END;
        }
      });

      _socket.set_close_handler([this](websocketpp::connection_hdl conn_hdl) {
        for (size_t i = 0; i < _connections.size(); i++) {
          websocketpp::connection_hdl hdl = _connections[i];
          if (_socket.get_con_from_hdl(conn_hdl) ==
              _socket.get_con_from_hdl(hdl)) {
            _connections.erase(_connections.begin() + i);
          }
        }
      });

      _socket.set_message_handler([this](websocketpp::connection_hdl conn_hdl,
                                         Server::message_ptr msg) {
        try {
          Response resp = _on_msg(msg->get_payload());
          if (resp.type == ResponseType::FORWARD) {
            resp.text = msg->get_payload();
          }
          handleResponse(resp, conn_hdl);
        } catch (const std::exception &e) {
          LOG_ERROR << "Error while handling a message: " << e.what()
                    << LOG_END;
        } catch (...) {
          LOG_ERROR << "Unknown error while handling a message." << LOG_END;
        }
      });

      _socket.set_tls_init_handler([](websocketpp::connection_hdl conn)
                                       -> ssl_ctx_pt {
        ssl_ctx_pt ctx =
            std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
        try {
          ctx->set_options(
              asio::ssl::context::default_workarounds |
              asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
              asio::ssl::context::no_tlsv1 | asio::ssl::context::single_dh_use);
          ctx->use_certificate_chain_file("./cert/certificate.pem");
          ctx->use_private_key_file("./cert/key.pem", asio::ssl::context::pem);
          ctx->use_tmp_dh_file("./cert/dh1024.pem");
        } catch (const std::exception &e) {
          LOG_ERROR << "Error during tls initializtion " << e.what() << LOG_END;
        }
        LOG_DEBUG << "Initialized the ssl context for the web _socket server"
                  << LOG_END;
        return ctx;
      });

      _socket.listen(8081);
      _socket.start_accept();
      LOG_INFO << "Starting the wss server on 8081" << LOG_END;
      _socket.run();
    } catch (const std::exception &e) {
      LOG_ERROR << "A socket error occured in the wss server: " << e.what()
                << LOG_END;
      std::this_thread::sleep_for(std::chrono::seconds(15));
    }
  }
}

void WebSocketServer::handleResponse(const Response &response,
                                     websocketpp::connection_hdl &initiator) {
  switch (response.type) {
    case ResponseType::FORWARD:
    case ResponseType::BROADCAST: {
      for (websocketpp::connection_hdl other : _connections) {
        try {
          _socket.send(other, response.text, websocketpp::frame::opcode::text);
        } catch (const websocketpp::exception &e) {
          LOG_WARN << "Unable to forward a message to one of the clients."
                   << LOG_END;
        }
      }
    } break;
    case ResponseType::RETURN: {
      try {
        _socket.send(initiator, response.text,
                     websocketpp::frame::opcode::text);
      } catch (const websocketpp::exception &e) {
        LOG_WARN << "Unable to send a reply." << LOG_END;
      }
    } break;
    case ResponseType::SILENCE:
      // Do nothing
      break;
  }
}
