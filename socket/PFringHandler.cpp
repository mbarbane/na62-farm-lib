/*
 * PFringHandler.cpp
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunzej@cern.ch)
 */

#include "PFringHandler.h"

namespace na62 {
ntop::PFring ** PFringHandler::queueRings_;
uint16_t PFringHandler::numberOfQueues_;

std::atomic<uint64_t> PFringHandler::bytesReceived_(0);
std::atomic<uint64_t> PFringHandler::packetsReceived_(0);
std::string PFringHandler::deviceName_ = "";
boost::thread* PFringHandler::ARPThread;
boost::mutex PFringHandler::sendMutex_;

void PFringHandler::Initialize(std::string deviceName) {
	deviceName_ = deviceName;
	u_int32_t flags = 0;
	//flags |= PF_RING_REENTRANT;
	flags |= PF_RING_LONG_HEADER;
	flags |= PF_RING_PROMISC;
	flags |= PF_RING_DNA_SYMMETRIC_RSS; /* Note that symmetric RSS is ignored by non-DNA drivers */

	const int snaplen = 128;

	ntop::PFring* tmpRing = new ntop::PFring((char*) deviceName.data(), snaplen,
			flags);
	numberOfQueues_ = tmpRing->get_num_rx_channels();
	queueRings_ = new ntop::PFring *[numberOfQueues_];

	if (numberOfQueues_ > 1) {
//		flags |= PF_RING_REENTRANT;
		delete tmpRing;
		tmpRing = new ntop::PFring((char*) deviceName.data(), snaplen, flags);
	}

	for (uint8_t i = 0; i < numberOfQueues_; i++) {
		std::string queDeviceName = deviceName;
		if (i > 0) {
			queDeviceName = deviceName + "@"
					+ boost::lexical_cast<std::string>((int) i);
			tmpRing = new ntop::PFring((char*) queDeviceName.data(), snaplen,
					flags);
		}

		/*
		 * If numberOfSendqueues is >1 tmpRing is the pfRing object for queue 0 -> put into queueRings_[0]
		 */
		queueRings_[i] = tmpRing;

		if (tmpRing->enable_ring() >= 0) {
			mycout(
					"Successfully opened device "
							+ boost::lexical_cast<std::string>(
									tmpRing->get_device_name()) + " with "
							+ boost::lexical_cast<std::string>(
									(int ) tmpRing->get_num_rx_channels())
							+ " rx queues");
		} else {
			mycerr(
					"Unable to open device "
							+ boost::lexical_cast<std::string>(queDeviceName)
							+ "! Is pf_ring not loaded or do you use quick mode and have already a socket bound to this device?!");
			exit(1);
		}
	}

	ARPThread = new boost::thread(boost::bind(&PFringHandler::StartARPThread));
}

void PFringHandler::StartARPThread() {

	struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
			GetMyMac(), GetMyIP());
	/*
	 * Periodically send a gratuitous ARP packets
	 */
	while (true) {
		SendPacket(arp.data, arp.length);
		boost::this_thread::sleep(boost::posix_time::seconds(60));
	}
}

void PFringHandler::Shutdown() {
	ARPThread->interrupt();
	delete ARPThread;
	for (uint8_t i = 0; i < numberOfQueues_; i++) {
		delete queueRings_[i];
	}
	delete[] queueRings_;
}

void PFringHandler::PrintStats() {
	pfring_stat stats = { 0 };
	mycout("Ring\trecv\tdrop");
	for (int i = 0; i < numberOfQueues_; i++) {
		queueRings_[i]->get_stats(&stats);
		mycout(i << " \t" << stats.recv << "\t" << stats.drop);
	}

//
//	mycout("Absolute Stats: [" << pfringStat.recv<< " pkts rcvd][" << pfringStat.drop << " pkts dropped]");
//	mycout(
//			"Total Pkts=" << (unsigned int) (pfringStat.recv+pfringStat.drop) << "/Dropped=" << (pfringStat.recv== 0 ? 0 : (float) (pfringStat.drop * 100) / (float) (pfringStat.recv + pfringStat.drop)));
}
} /* namespace na62 */