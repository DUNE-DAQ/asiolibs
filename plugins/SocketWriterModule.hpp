/**
 * @file SocketWriterModule.hpp Boost.Asio-based socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_
#define ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_

#include "GenericReceiverConcept.hpp"

#include "appfwk/DAQModule.hpp"
#include "utilities/ReusableThread.hpp"

#include "appmodel/SocketDataWriterModule.hpp"

#include <boost/asio.hpp>

#include <string>
#include <memory>
#include <vector>
#include <queue>
#include <map>

namespace dunedaq::asiolibs {

class SocketWriterModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief Default receiver timeout in ms
   */
  static constexpr auto default_receiver_timeout_ms = 10;

  /**
   * @brief SocketWriterModule constructor
   * @param name DAQ module instance name
   */
  explicit SocketWriterModule(const std::string& name);
  ~SocketWriterModule() = default;

  SocketWriterModule(const SocketWriterModule&) = delete; ///< SocketWriterModule is not copy-constructible
  SocketWriterModule& operator=(const SocketWriterModule&) =
    delete;                                                  ///< SocketWriterModule is not copy-assignable
  SocketWriterModule(SocketWriterModule&&) = delete; ///< SocketWriterModule is not move-constructible
  SocketWriterModule& operator=(SocketWriterModule&&) =
    delete; ///< SocketWriterModule is not move-assignable

  /**
   * @brief Handles initialization on boot
   * @param mcfg DAQ configuration data
   */
  void init(const std::shared_ptr<appfwk::ConfigurationManager> mcfg) override;

private:
  enum class SocketType
  {
    TCP,
    UDP,
    INVALID
  };

  struct SocketStats
  {
    /**
     * @brief Total number of received payloads
     */
    std::atomic<uint64_t> sum_payloads{ 0 }; // NOLINT(build/unsigned)

    /**
     * @brief Incremental number of received payloads 
     */
    std::atomic<uint64_t> num_payloads{ 0 }; // NOLINT(build/unsigned)

    /**
     * @brief Total number of received bytes 
     */
    std::atomic<uint64_t> sum_bytes{ 0 }; // NOLINT(build/unsigned)

    /**
     * @brief Timeout on data inputs 
     */
    std::atomic<uint64_t> queue_timeout_count{ 0 };   

    /**
     * @brief Rate of consumed packets 
     */
    std::atomic<double> rate_payloads_consumed{ 0 };
    
    /**
     * @brief Counts packets since last opmon data generation
     */
    std::atomic<int> stats_packet_count{ 0 };    
  };

  struct WriterInfo
  {
    /**
     * @brief Source IP address
     */
    std::string local_ip;

    /**
     * @brief Source port number
     */
    uint32_t local_port; // NOLINT(build/unsigned)

    /**
     * @brief Destination IP address
     */
    std::string remote_ip;

    /**
     * @brief Destination port number
     */
    uint32_t remote_port; // NOLINT(build/unsigned)

    /**
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> socket_stats;
  };

  class TCPWriter
  {
  public:
    /**
     * @brief Creates and connects a TCP socket
     * @param io_context I/O context for socket creation and coroutine spawn
     * @param writer_info TCP writer info
     * @throws boost::system::system_error on failure
     */
    void configure(boost::asio::io_context& io_context, std::shared_ptr<WriterInfo> writer_info);

    /**
     * @brief Enqueues payload
     * @param payload Payload to send
     */
    void enqueue(GenericReceiverConcept::TypeErasedPayload&& payload);

    /**
     * @brief Closes the socket
     */
    void stop();

    /**
     * @brief Get socket statistics
     * @return Statistics of socket traffic
     */    
    std::shared_ptr<SocketStats> get_socket_stats() const;    

  private:
    /**
     * @brief Asynchronously sends payloads to the socket in a loop
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start();

    /**
     * @brief TCP socket
     */
    std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;

    /**
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> m_socket_stats;

    /**
     * @brief Payloads waiting to be sent (ensures no simultaneous async_write calls)
     */
    std::queue<GenericReceiverConcept::TypeErasedPayload> m_payloads;

    /**
    * @brief I/O context for socket operations
    */
    boost::asio::io_context* m_io_context;

    /**
    * @brief Ensures no race on queue
    */    
    std::shared_ptr<boost::asio::strand<boost::asio::io_context::executor_type>> m_strand;
  };

  class UDPWriter
  {
  public:
    /**
     * @brief Creates a UDP socket
     * @param io_context I/O context for socket creation and coroutine spawn
     * @param writer_info UDP writer info
     */
    void configure(boost::asio::io_context& io_context, std::shared_ptr<WriterInfo> writer_info);

    /**
     * @brief Enqueues payload
     * @param payload Payload to send
     */
    void enqueue(GenericReceiverConcept::TypeErasedPayload&& payload);

    /**
     * @brief Closes the socket
     */
    void stop();

    /**
     * @brief Get socket statistics
     * @return Statistics of socket traffic
     */    
    std::shared_ptr<SocketStats> get_socket_stats() const;

  private:
    /**
     * @brief Asynchronously sends payloads to the socket in a loop
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start();

    /**
     * @brief UDP socket
     */
    std::unique_ptr<boost::asio::ip::udp::socket> m_socket;

    /**
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> m_socket_stats;

    /**
     * @brief Payloads waiting to be sent (ensures no simultaneous async_write calls)
     */
    std::queue<GenericReceiverConcept::TypeErasedPayload> m_payloads;

    /**
    * @brief I/O context for socket operations
    */
    boost::asio::io_context* m_io_context;    

    /**
    * @brief Remote endpoint
    */
    boost::asio::ip::udp::endpoint m_remote_endpoint;

    /**
    * @brief Ensures no race on queue
    */    
    std::shared_ptr<boost::asio::strand<boost::asio::io_context::executor_type>> m_strand;    
  };

  // Commands
  void do_configure(const CommandData_t&);
  void do_start(const CommandData_t&);
  void do_stop(const CommandData_t&);

  void generate_opmon_data() override;

  /**
   * @brief Converts a socket type string to an enum
   * @param socket_type Socket type as a string
   * @return Corresponding SocketType enum
   */
  SocketType string_to_socket_type(const std::string& socket_type) const;

  /**
   * @brief Gets dal inputs
   * @param mdal SocketDataWriterModule dal
   */   
  void get_dal_inputs(const dunedaq::appmodel::SocketDataWriterModule* mdal);

  /**
   * @brief Data consume thread function
   */   
  void run_consume();

  /**
   * @brief I/O context for socket operations
   */
  boost::asio::io_context m_io_context;

  /**
   * @brief Prevents I/O context from exiting prematurely
   */
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work_guard;

  /**
   * @brief Socket writer
   */
  std::shared_ptr<std::variant<TCPWriter, UDPWriter>> m_writer;

  /**
   * @brief Background thread to keep the I/O context running
   */
  std::jthread m_io_thread;

  /**
   * @brief Socket writer info
   */
  std::shared_ptr<WriterInfo> m_writer_info;

  // RECEIVER  
  /**
   * @brief Generic receiver
   */    
  std::shared_ptr<GenericReceiverConcept> m_receiver;

  /**
   * @brief Receiver timeout in ms
   */    
  std::chrono::milliseconds m_receiver_timeout_ms{ default_receiver_timeout_ms };

  using sid_to_writer_map_t = std::map<uint32_t, std::shared_ptr<std::variant<TCPWriter, UDPWriter>>>; // NOLINT(build/unsigned)
  /**
   * @brief Source ID to writer map
   */   
  sid_to_writer_map_t m_sid_to_writer;
  
  // CONSUMER
  /**
   * @brief Data consume thread
   */     
  utilities::ReusableThread m_consumer_thread;

  /**
   * @brief Whether consumer thread should continue
   */    
  std::atomic<bool> m_run_marker { false };

  // RUN START T0
  /**
   * @brief Timestamp used to measure time between opmon reports
   */   
  std::chrono::time_point<std::chrono::steady_clock> m_t0;  
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_
