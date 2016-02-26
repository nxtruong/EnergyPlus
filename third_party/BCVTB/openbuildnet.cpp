/* -*- mode: C++; indent-tabs-mode: nil; -*- */
/** \file
 * \brief Main code to integrate OBN into EnergyPlus.
 *
 * This file is part of the openBuildNet simulation framework
 * (OBN-Sim) developed at EPFL.
 *
 * \author Truong X. Nghiem (xuan.nghiem@epfl.ch)
 */

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>

#include <obnnode.h>
#include "openbuildnet.h"

#define NDBLMAX 1024    // The maximum number of double values that can be read; this MUST be the same number hard-coded in ExternalInterface.cc

using namespace OBNnode;

namespace EnergyPlus {
    namespace ExternalInterface {
        
        // Some options
        bool quitIfOBNTerminates = false;   ///< whether E+ should quit if OBN terminates
        int default_obn_timeout = -1;   ///< The default timeout value
        
        EPlusSignalToOBN eplus_signal_to_obn = OBNSIG_NONE;       ///< The signal from E+ to OBN.
        std::mutex eplus_signal_to_obn_mutex;       ///< Mutex to access the signal
        std::condition_variable eplus_signal_to_obn_cond;   ///< Condition variable to wait for the signal

        OBNSignalToEPlus obn_signal_to_eplus = EPSIG_NONE;       ///< The signal from OBN to E+.
        std::mutex obn_signal_to_eplus_mutex;       ///< Mutex to access the signal
        std::condition_variable obn_signal_to_eplus_cond;   ///< Condition variable to wait for the signal
        
        /** Set the signal from OBN to E+. */
        void signalEPlus(OBNSignalToEPlus sig) {
            {
                std::lock_guard<std::mutex> mylock(obn_signal_to_eplus_mutex);
                obn_signal_to_eplus = sig;
            }
            obn_signal_to_eplus_cond.notify_all();
        }
        
        /** Wait for a signal from EnergyPlus. */
        EPlusSignalToOBN waitforEPlusSignal() {
            std::unique_lock<std::mutex> mylock(eplus_signal_to_obn_mutex);
            if (eplus_signal_to_obn != OBNSIG_NONE) {
                return eplus_signal_to_obn;
            }
            
            eplus_signal_to_obn_cond.wait(mylock, []{ return eplus_signal_to_obn != OBNSIG_NONE; });
            return eplus_signal_to_obn;
        }
        
        /** Reset the signal from EnergyPlus. */
        void resetEPlusSignal() {
            std::lock_guard<std::mutex> mylock(eplus_signal_to_obn_mutex);
            eplus_signal_to_obn = OBNSIG_NONE;
        }
        
        OBNSignalToEPlus getOBNSignal() {
            std::lock_guard<std::mutex> mylock(obn_signal_to_eplus_mutex);
            return obn_signal_to_eplus;
        }
        
        std::string getOBNSignalName(OBNSignalToEPlus sig) {
            switch (sig) {
                case EPSIG_NONE:
                    return "NONE";
                    
                case EPSIG_START:
                    return "START";
                    
                case EPSIG_Y:
                    return "UPDATE_Y";
                    
                case EPSIG_X:
                    return "UPDATE_X";
                    
                case EPSIG_TERM:
                    return "TERMINATE";
                    
                case EPSIG_QUIT:
                    return "QUIT";
                    
                case EPSIG_TIMEOUT:
                    return "TIMEOUT";
                    
                default:
                    return "INVALID";
            }
        }
        
        void resetOBNSignal() {
            std::lock_guard<std::mutex> mylock(obn_signal_to_eplus_mutex);
            obn_signal_to_eplus = EPSIG_NONE;
        }
        
        void signalOBN_TERM() {
            signalOBN(OBNSIG_TERM);
        }
        
        void signalOBN_EXIT() {
            signalOBN(OBNSIG_EXIT);
        }
        
        OBNSignalToEPlus waitforOBNSignal(int timeout) {
            std::unique_lock<std::mutex> mylock(obn_signal_to_eplus_mutex);
            if (obn_signal_to_eplus != EPSIG_NONE) {
                return obn_signal_to_eplus;
            }
            
            // Get the actual timeout value we want to use
            if (timeout <= 0) {
                timeout = default_obn_timeout;
            }
            
            // Wait for the signal to be not EPSIG_NONE, with or without timeout
            if (timeout <= 0) {
                obn_signal_to_eplus_cond.wait(mylock, []{ return obn_signal_to_eplus != EPSIG_NONE; });
                return obn_signal_to_eplus;
            } else {
                if (obn_signal_to_eplus_cond.wait_for(mylock, std::chrono::seconds(timeout), []{ return obn_signal_to_eplus != EPSIG_NONE; })) {
                    return obn_signal_to_eplus;
                } else {
                    return EPSIG_TIMEOUT;
                }
            }
        }
        
        void setOBNTimeout(int timeout) {
            default_obn_timeout = timeout;
        }
        

        /** The OBN node class for EnergyPlus. */
        class MQTTNodeEPlus: public MQTTNodeBase {
            /** Input and output ports for the node. */
            MQTTInput< OBN_PB, obn_vector<double> > m_double_input;     // All double inputs
            MQTTOutput< OBN_PB, obn_vector<double> > m_double_output;   // All double outputs
            
            /** Ask EnergyPlus to stop unexpectedly. */
            void askEnergyPlusToQuit() {
                resetEPlusSignal();
                signalEPlus(EPSIG_QUIT);       // Forward the signal to EPlus
                waitforEPlusSignal();           // Wait for the ACK to ensure that EnergyPlus has registered the request
            }
            
        public:
            bool initialize();
            
            MQTTNodeEPlus(const std::string& t_name, const std::string& t_ws):
            MQTTNodeBase(t_name, t_ws), m_double_input("in"), m_double_output("out")
            { }
            
            void handleEPlusSignal();       ///< Handle some signals from EnergyPlus
            
            /** Write the output values to the output port.
             \return negative value if error; otherwise the number of values sent.
             */
            int setOutputValues(std::size_t nDbl, double dblVals[]) {
                auto& v = *m_double_output;
                v.resize(nDbl);
                if (nDbl > 0) {
                    std::copy_n(dblVals, nDbl, v.data());
                }
                return nDbl;
            }
            
            /** Read the input values from the input port.
             The arrays to write the values to are already allocated by EnergyPlus.
             \return non-zero value if error.
             */
            int getInputValues(int *nDblRea, double dblValRea[]) {
                // Directly access the internal data to avoid copying data multiple times
                *nDblRea = 0;
                auto la = m_double_input.lock_and_get();
                auto sz = la->size();
                if (sz > NDBLMAX) {
                    return -1;
                }
                *nDblRea = sz;
                if (sz > 0) {
                    std::copy_n(la->data(), sz, dblValRea);
                }
                return 0;
            }
            
            /** \brief Callback for UPDATE_Y event */
            virtual void onUpdateY(updatemask_t m) override;
            
            /** \brief Callback for UPDATE_X event */
            virtual void onUpdateX(updatemask_t m) override;
            
            /** \brief Callback to initialize the node before each simulation. */
            virtual void onInitialization() override;
            
            /** \brief Callback before the node's current simulation is terminated. */
            virtual void onTermination() override;
            
            /** Callback for error when parsing the raw binary data into a structured message (e.g. ProtoBuf or JSON) */
            virtual void onRawMessageError(const PortBase * port, const std::string& info) override {
                _node_state = NODE_ERROR;
                //auto msg = "Error while parsing the raw message from port: " + port->fullPortName() + " (" + info + ")";
                //reportError("MQTTNODE:communication", msg.c_str());
                askEnergyPlusToQuit();
            }
            
            /** Callback for error when reading the values from a structured message (e.g. ProtoBuf or JSON), e.g. if the type or dimension is invalid. */
            virtual void onReadValueError(const PortBase * port, const std::string& info) override {
                _node_state = NODE_ERROR;
                //auto msg = "Error while extracting value from message for port: " + port->fullPortName() + " (" + info + ")";
                //reportError("MQTTNODE:communication", msg.c_str());
                askEnergyPlusToQuit();
            }
            
            /** Callback for error when sending the values (typically happens when serializing the message to be sent). */
            virtual void onSendMessageError(const PortBase * port, const std::string& info) override {
                _node_state = NODE_ERROR;
                //auto msg = "Error while sending a value from port: " + port->fullPortName() + " (" + info + ")";
                //reportError("MQTTNODE:communication", msg.c_str());
                askEnergyPlusToQuit();
            }
            
            /** Callback for error interacting with the SMN and openBuildNet system.  Used for serious errors.
             \param msg A string containing the error message.
             */
            virtual void onOBNError(const std::string& msg) override {
                _node_state = NODE_ERROR;
                //reportError("MQTTNODE:openBuildNet", msg.c_str());
                askEnergyPlusToQuit();
            }
            
            /** Callback for warning issues interacting with the SMN and openBuildNet system, e.g. an unrecognized system message from the SMN.  Usually the simulation may continue without any serious consequence.
             \param msg A string containing the warning message.
             */
            virtual void onOBNWarning(const std::string& msg) override {
                //reportWarning("MQTTNODE:openBuildNet", msg.c_str());
            }
        };
        
        /** \brief The class that runs the OBN node thread. */
        class EPlusOBNThread {
            std::thread m_thread;
            
            /** Main function of the thread. */
            void threadMain();
            
        public:
            MQTTNodeEPlus m_obnnode;
            
            EPlusOBNThread(const std::string& t_name, const std::string& t_ws = ""): m_obnnode(t_name, t_ws)
            {
            }
            
            ~EPlusOBNThread() {
                // If the thread is running, we need to stop it properly
                stopThread();
            }
            
            /** Start the OBN node thread. */
            bool startThread() {
                if (m_thread.joinable()) {
                    // If the thread is already running, we can't start it again
                    return false;
                }
                
                // Start the node and the thread
                if (!m_obnnode.initialize()) {
                    return false;
                }
                
                m_thread = std::thread(&EPlusOBNThread::threadMain, this);
                
                return true;
            }
            
            /** Stop the OBN node thread properly. */
            void stopThread() {
                if (m_thread.joinable()) {
                    // Signal the thread to exit
                    signalOBN(OBNSIG_EXIT);
                    m_thread.join();
                }
            }
            
        };
        
        std::unique_ptr<EPlusOBNThread> obn_thread;
        
        bool initOBNNode(const char * docname) {
            if (!docname) return false;
            if (!obn_thread) {
                std::string node_name;
                std::string workspace;
                std::string comm;
                std::string comm_config;
                const std::string spacechars(" \t");
                
                // Read the config file
                std::ifstream configfile(docname);
                bool success = configfile.good();
                if (success) {
                    std::string oneline;
                    
                    // The first line: communication settings
                    if ((success = !std::getline(configfile, oneline).fail())) {
                        // Extract the first word
                        auto pos = oneline.find_first_of(spacechars);
                        if (pos == std::string::npos) {
                            comm = oneline;
                        } else {
                            comm = oneline.substr(0, pos);
                            // Extract the comm config if there is: find the first non-blank char
                            pos = oneline.find_first_not_of(spacechars, pos);
                            if (pos != std::string::npos) {
                                comm_config = oneline.substr(pos);
                            }
                        }
                    }
                    
                    // The second line: node settings
                    if (success && (success = !std::getline(configfile, oneline).fail())) {
                        // Extract the first word -> node's name
                        auto pos = oneline.find_first_of(spacechars);
                        if (pos == std::string::npos) {
                            node_name = oneline;
                        } else {
                            node_name = oneline.substr(0, pos);
                            // Extract the optional workspace name: find the first non-blank char
                            pos = oneline.find_first_not_of(spacechars, pos);
                            if (pos != std::string::npos) {
                                workspace = oneline.substr(pos);
                            }
                        }
                    }
                    
                    // The remaining lines are optional
                    while (success && !configfile.eof()) {
                        // Get the next line
                        success = !std::getline(configfile, oneline).fail();
                        if (success) {
                            // Extract the first word, which is the option name
                            auto pos = oneline.find_first_of(spacechars);
                            std::string theOption(oneline), theRest;

                            if (pos != std::string::npos) {
                                theOption = oneline.substr(0, pos);
                                if (oneline.size() >= pos+2) {
                                    theRest = OBNsim::Utils::trim(oneline.substr(pos+1));
                                }
                            }
                            theOption = OBNsim::Utils::toLower(OBNsim::Utils::trim(theOption));
                            
                            // Check which option we have
                            if (theOption == "quitifobnstops") {
                                // quitIfOBNStops: quit E+ if OBN stops (default is false).
                                quitIfOBNTerminates = true;
                            } else if (theOption == "timeout") {
                                // Set the default timeout value (default is -1, i.e. wait indefinitely)
                                // This is an integer as the number of seconds; if it's absent, it won't change the timeout
                                if (!theRest.empty()) {
                                    setOBNTimeout(atoi(theRest.c_str()));
                                }
                            } else {
                                // Unknown option -> for now we just ignore it but we should instead print out some error
                            }
                        }
                        success = true;
                    }
                }

                if (!success) {
                    return false;
                }
                
                // Check the communication
                comm = OBNsim::Utils::toLower(OBNsim::Utils::trim(comm));
                node_name = OBNsim::Utils::trim(node_name);
                if (comm == "mqtt") {
                    // Check the node name
                    if (!OBNsim::Utils::isValidNodeName(node_name)) {
                        return false;
                    }
                    
                    obn_thread.reset(new EPlusOBNThread(node_name, workspace));
                    
                    // Config the MQTT if needed
                    comm_config = OBNsim::Utils::trim(comm_config);
                    if (!comm_config.empty()) {
                        obn_thread->m_obnnode.setServerAddress(comm_config);
                    }
                    
                    // Start the thread
                    return obn_thread->startThread();
                } else {
                    // Only MQTT is supported
                    return false;
                }
            }
            
            return true;
        }
        
        void stopOBNNode() {
            if (!obn_thread) {
                obn_thread->stopThread();
            }
        }

        void signalOBN(EPlusSignalToOBN sig) {
            {
                std::lock_guard<std::mutex> mylock(eplus_signal_to_obn_mutex);
                eplus_signal_to_obn = sig;
            }
            eplus_signal_to_obn_cond.notify_all();
            if (sig != OBNSIG_DONE && obn_thread && sig != OBNSIG_NONE) {
                // Push an event to the node's queue to process the signal
                obn_thread->m_obnnode.postCallbackEvent(std::bind(&MQTTNodeEPlus::handleEPlusSignal, &obn_thread->m_obnnode));
            }
        }
        
        
        
        /** Run the OBN Node thread, communicating with OBN system as well as EnergyPlus (via signals).
         This thread will terminate when the OBN system terminates or when it receives the EXIT signal from E+.
         */
        void EPlusOBNThread::threadMain() {
            //            std::unique_lock<std::mutex> mylock(eplus_signal_to_obn_mutex);
            //            eplus_signal_to_obn_cond.wait(mylock, []{ return eplus_signal_to_obn == OBNSIG_EXIT; });
            m_obnnode.run();
        }
        
        /** Initialize the node for EPlus. */
        bool MQTTNodeEPlus::initialize() {
            if (!openSMNPort()) {
                return false;
            }
            
            // Add the output port to the nod
            return addInput(&m_double_input) && addOutput(&m_double_output);
        }
        
        /** \brief Callback for UPDATE_Y event */
        void MQTTNodeEPlus::onUpdateY(updatemask_t m) {
            signalEPlus(EPSIG_Y);       // Forward the signal to EPlus
            auto sig = waitforEPlusSignal();
            // Only reset the signal if the update was done properly, otherwise let the main function handle the signal
            if (sig == OBNSIG_DONE) {
                resetEPlusSignal();
            }
        }
        
        /** \brief Callback for UPDATE_X event */
        void MQTTNodeEPlus::onUpdateX(updatemask_t m) {
            signalEPlus(EPSIG_X);       // Forward the signal to EPlus
            auto sig = waitforEPlusSignal();
            // Only reset the signal if the update was done properly, otherwise let the main function handle the signal
            if (sig == OBNSIG_DONE) {
                resetEPlusSignal();
            }
        }
        
        /** \brief Callback to initialize the node before each simulation. */
        void MQTTNodeEPlus::onInitialization() {
            signalEPlus(EPSIG_START);       // Forward the signal to EPlus
            auto sig = waitforEPlusSignal();
            // Only reset the signal if the update was done properly, otherwise let the main function handle the signal
            if (sig == OBNSIG_DONE) {
                resetEPlusSignal();
            }
        }
        
        /** \brief Callback before the node's current simulation is terminated. */
        void MQTTNodeEPlus::onTermination() {
            signalEPlus(EPSIG_TERM);       // Forward the signal to EPlus
        }
        
        /** Handle signal from EnergyPlus, except for DONE and NONE. */
        void MQTTNodeEPlus::handleEPlusSignal() {
            EPlusSignalToOBN sig;
            {
                std::lock_guard<std::mutex> mylock(eplus_signal_to_obn_mutex);
                sig = eplus_signal_to_obn;
                eplus_signal_to_obn = OBNSIG_NONE;
            }
            switch (sig) {
                case OBNSIG_TERM:
                case OBNSIG_EXIT:
                    // E+ terminates the simulation, so OBN should also stop
                    stopSimulation();
                    break;
                    
                default:
                    break;
            }
        }
        
        /** Process the common signal from OBN to E+ in exchangewithsocket().
         \param sig The signal from OBN.
         \param flaRea To set an appropriate flag for E+.
         \return The return value (negative if error of communication, non-negative if success).
         */
        int processOBNSignal(EnergyPlus::ExternalInterface::OBNSignalToEPlus sig, int *flaRea) {
            switch (sig) {
                case EnergyPlus::ExternalInterface::EPSIG_TERM:
                    // Normal termination
                    *flaRea = 1;
                    return 0;
                    
                case EnergyPlus::ExternalInterface::EPSIG_QUIT:
                    // Abnormal termination
                    *flaRea = -1;
                    return 0;
                    
                case EnergyPlus::ExternalInterface::EPSIG_TIMEOUT:
                    return -21;
                    
                default:
                    // If we reach this, it means the received signal is unexpected (of the wrong type)
                    return -22;
            }
        }
        
        
        /////////////////////////////////////////////////////////////////
        /// Exchanges data with openBuildNet.
        ///
        /// Clients can call this method to exchange data through the socket.
        ///\param flaWri Communication flag to write to the socket stream.
        ///\param flaRea Communication flag read from the socket stream.
        ///\param nDblWri Number of double values to write.
        ///\param nDblRea Number of double values to read.
        ///\param dblValWri Double values to write.
        ///\param simTimRea Current simulation time in seconds read from socket.
        ///\param dblValRea Double values read from socket.
        ///\return The exit value of \c send or \c read, or a negative value if an error occured.
        
        int exchangedoublewithOBN(const int *flaWri, int *flaRea,
                                  const int *nDblWri,
                                  int *nDblRea,
                                  double dblValWri[],
                                  double *simTimRea,
                                  double dblValRea[]){
            
            *flaRea = 0;    // Initialize to normal status
            
            if (!EnergyPlus::ExternalInterface::obn_thread) {
                return -1;
            }
            
            int retVal = 0;
            
            // Wait for UPDATE_Y from OBN before sending out the values
            auto sig = EnergyPlus::ExternalInterface::waitforOBNSignal();
            EnergyPlus::ExternalInterface::resetOBNSignal();
            if (sig != EnergyPlus::ExternalInterface::EPSIG_Y) {
                return processOBNSignal(sig, flaRea);
            }
            
            // Set the values of the output port
            retVal = EnergyPlus::ExternalInterface::obn_thread->m_obnnode.setOutputValues(*nDblWri, dblValWri);
            
            EnergyPlus::ExternalInterface::signalOBN(EnergyPlus::ExternalInterface::OBNSIG_DONE);   // ACK to OBN
            
            if ( retVal >= 0 ){
                // Wait for UPDATE_X from OBN before reading the input values
                sig = EnergyPlus::ExternalInterface::waitforOBNSignal();
                EnergyPlus::ExternalInterface::resetOBNSignal();
                if (sig != EnergyPlus::ExternalInterface::EPSIG_X) {
                    return processOBNSignal(sig, flaRea);
                }
                
                // Obtain the input values
                retVal = EnergyPlus::ExternalInterface::obn_thread->m_obnnode.getInputValues(nDblRea, dblValRea);
                
                // Get the current simulation time from OBN for EnergyPlus
                *simTimRea = EnergyPlus::ExternalInterface::obn_thread->m_obnnode.currentSimulationTime<std::chrono::seconds>();
                
                // The flag flaRea is already set at the beginning
                
                EnergyPlus::ExternalInterface::signalOBN(EnergyPlus::ExternalInterface::OBNSIG_DONE);   // ACK to OBN
                
            }
            return retVal;
        }
    }
}
