#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/rip-helper.h"
#include "ns3/error-model.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/string.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RoutingComparisonScenario");

// Global variables for metrics collection
uint32_t totalPacketsReceived = 0;
const double linkFailureTime = 20.0;

// Packet sink callback to measure reception and convergence
void PacketReceived(Ptr<const Packet> packet, const Address &address)
{
    totalPacketsReceived++;
}

// Function to induce link failure
void CauseLinkFailure(Ptr<NetDevice> dev0, Ptr<NetDevice> dev1)
{
    NS_LOG_UNCOND("--- LINK FAILURE EVENT at " << Simulator::Now().GetSeconds() << "s ---");
    // Create an error model to drop all packets
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(1.0));
    em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    
    // Apply the error model to both ends of the link
    dev0->SetAttribute("ReceiveErrorModel", PointerValue(em));
    dev1->SetAttribute("ReceiveErrorModel", PointerValue(em));
}

// Function to run a single simulation iteration with a specific routing protocol
void RunSimulation(bool useRip)
{
    NS_LOG_UNCOND("--- Starting Simulation with " << (useRip ? "RIP (Distance Vector)" : "Global Routing (Link State)") << " ---");

    // Reset metrics
    totalPacketsReceived = 0;

    // a dummy node with Id 0 to force NodeList to start from 1.
    NodeContainer dummy;
    dummy.Create(1);

    // 1. Create nodes (N1 to N6)
    NodeContainer nodes;
    nodes.Create(6);
    Ptr<Node> n1 = nodes.Get(0);
    Ptr<Node> n2 = nodes.Get(1);
    Ptr<Node> n3 = nodes.Get(2);
    Ptr<Node> n4 = nodes.Get(3);
    Ptr<Node> n5 = nodes.Get(4);
    Ptr<Node> n6 = nodes.Get(5);

    // 2. Configure Point-to-Point links
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer d_n1n2 = p2p.Install(n1, n2);
    NetDeviceContainer d_n2n3 = p2p.Install(n2, n3); // Link to fail
    NetDeviceContainer d_n3n6 = p2p.Install(n3, n6);
    NetDeviceContainer d_n1n4 = p2p.Install(n1, n4);
    NetDeviceContainer d_n4n5 = p2p.Install(n4, n5);
    NetDeviceContainer d_n5n6 = p2p.Install(n5, n6);
    NetDeviceContainer d_n2n4 = p2p.Install(n2, n4);

    // 3. Install Internet Stack
    InternetStackHelper stack;
    if (useRip) {
        RipHelper ripRouting;
        stack.SetRoutingHelper(ripRouting);
        stack.Install(nodes);

    } else {
        stack.Install(nodes);
    }

    // 4. Assign IP Addresses
    Ipv4AddressHelper address;
    address.SetBase("10.0.1.0", "255.255.255.0");
    address.Assign(d_n1n2);
    address.SetBase("10.0.2.0", "255.255.255.0");
    address.Assign(d_n2n3);
    address.SetBase("10.0.3.0", "255.255.255.0");
    address.Assign(d_n3n6);
    address.SetBase("10.0.5.0", "255.255.255.0");
    address.Assign(d_n4n5);
    address.SetBase("10.0.6.0", "255.255.255.0");
    address.Assign(d_n5n6);
    address.SetBase("10.0.7.0", "255.255.255.0");
    address.Assign(d_n2n4);

    // Update routing tables after IP assignment for RIP (GlobalRouting is automatic)
    if (!useRip) {
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    // 5. Setup Traffic (UDP from N1 to N6)
    uint16_t sinkPort = 8080;
    Address sinkAddress(InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory", sinkAddress);
    ApplicationContainer sinkApps = packetSinkHelper.Install(n6);
    
    // Get the PacketSink application and connect our trace sink
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
    sink->TraceConnectWithoutContext("Rx", MakeCallback(&PacketReceived));
    
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(300.0));
 
    Ipv4Address remoteIp = n6->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(); 
    Address remoteAddress(InetSocketAddress(remoteIp, sinkPort));

    OnOffHelper onoff("ns3::UdpSocketFactory", remoteAddress);
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("DataRate", StringValue("5Mbps")); // Generate substantial traffic
    onoff.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer sourceApps = onoff.Install(n1);
    sourceApps.Start(Seconds(1.0)); // Start traffic after a short initial convergence time
    sourceApps.Stop(Seconds(300.0));

    // 6. Schedule Link Failure (N2-N3 link)
    
    // Get the actual NetDevice pointers for the link N2-N3
    Ptr<NetDevice> devN2 = d_n2n3.Get(0);
    Ptr<NetDevice> devN3 = d_n2n3.Get(1);
    Simulator::Schedule(Seconds(linkFailureTime), &CauseLinkFailure, devN2, devN3);
    

    // 7. Run Simulation and collect metrics
    Simulator::Stop(Seconds(600.0));
    
    // Use FlowMonitor for more detailed analysis
    FlowMonitorHelper flowMonitor;
    Ptr<FlowMonitor> monitor = flowMonitor.Install(nodes);

    // Print routing tables at regular intervals in a file.
    
    if(useRip) {
        Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(
        "rip-routing.routes", std::ios::out);
        // For RIP
        RipHelper rip;
        // Print for a single node (e.g., n2) at t=10s, 60, 120, 180, 240, 300s
        rip.PrintRoutingTableAt(Seconds(10.0), n2, routingStream);
        rip.PrintRoutingTableAt(Seconds(60.0), n2, routingStream);
        rip.PrintRoutingTableAt(Seconds(120.0), n2, routingStream);
        rip.PrintRoutingTableAt(Seconds(180.0), n2, routingStream);
        rip.PrintRoutingTableAt(Seconds(240.0), n2, routingStream);
        rip.PrintRoutingTableAt(Seconds(300.0), n2, routingStream);

    } else {
        Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(
        "global-routing.routes", std::ios::out);
        Ipv4ListRoutingHelper listRouting;
        // Print for a single node (e.g., n2) at t=10s, 60, 120, 180, 240, 300s
        listRouting.PrintRoutingTableAt(Seconds(10.0), n2, routingStream);
        listRouting.PrintRoutingTableAt(Seconds(60.0), n2, routingStream);
        listRouting.PrintRoutingTableAt(Seconds(120.0), n2, routingStream);
        listRouting.PrintRoutingTableAt(Seconds(180.0), n2, routingStream);
        listRouting.PrintRoutingTableAt(Seconds(240.0), n2, routingStream);
        listRouting.PrintRoutingTableAt(Seconds(300.0), n2, routingStream);
    }
    

    Simulator::Run();

    // 8. Analyze Results
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowMonitor.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    
    double totalTxPackets = 0;
    double totalRxPackets = 0;
    double totalDelaySum = 0;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        if ( t.destinationAddress == remoteIp && t.destinationPort == sinkPort) // Match source N1 and destination N6 IP
        {
            totalTxPackets = i->second.txPackets;
            totalRxPackets = i->second.rxPackets;
            totalDelaySum += i->second.delaySum.GetSeconds();
        }
    }

    double packetDeliveryRatio = (totalTxPackets > 0) ? (totalRxPackets / totalTxPackets) * 100 : 0;
    double avgDelay = (totalRxPackets > 0) ? (totalDelaySum / totalRxPackets) : 0;

    NS_LOG_UNCOND("=====================================================");
    NS_LOG_UNCOND("Results for " << (useRip ? "RIP" : "Global Routing"));
    NS_LOG_UNCOND("Packet Delivery Ratio: " << packetDeliveryRatio << "%");
    NS_LOG_UNCOND("Average End-to-End Delay: " << avgDelay * 1000 << " ms");
    NS_LOG_UNCOND("Total Packets Received: " << totalPacketsReceived);
    NS_LOG_UNCOND("=====================================================");

    Simulator::Destroy();
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Run RIP simulation first
    RunSimulation(true); 
    
    // Run Global Routing (LS) simulation second
    RunSimulation(false); 

    return 0;
}
