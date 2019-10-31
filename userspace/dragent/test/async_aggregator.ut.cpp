#include "async_aggregator.h"
#include "scoped_config.h"
#include "watchdog_runnable_pool.h"
#include <gtest.h>
#include <iostream>

// make sure pushing a single PB through the aggregator works
TEST(async_aggregator, single)
{
	test_helpers::scoped_config<uint32_t> config("aggregator.samples_between_flush", 1);

	blocking_queue<std::shared_ptr<flush_data_message>> input_queue(10);
	blocking_queue<std::shared_ptr<flush_data_message>> output_queue(10);

	dragent::async_aggregator* aggregator = new dragent::async_aggregator(input_queue,
																		  output_queue,
																		  // stupid short timeout because aint nobody got time for waiting for cleanup!
																		  1);
	dragent::watchdog_runnable_pool pool;
	pool.start(*aggregator, 10);
	std::atomic<bool> sent_metrics(false);

	draiosproto::metrics* input = new draiosproto::metrics();
	std::string machine_id = "zipperbox";
	input->set_machine_id(machine_id);

	uint32_t timestamp = 1;
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields
	for (uint32_t i = 0; output_queue.size() == 0 && i < 5000; ++i)
	{
		usleep(1000);
	}

	ASSERT_EQ(output_queue.size(), 1);
	std::shared_ptr<flush_data_message> output;
	bool ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_ts, timestamp);
	EXPECT_EQ(output->m_metrics_sent, &sent_metrics);
	EXPECT_EQ(output->m_metrics->machine_id(), machine_id);

	// not applicable to aggregated output
	EXPECT_EQ(output->m_nevts, 0);
	EXPECT_EQ(output->m_num_drop_events, 0);
	EXPECT_EQ(output->m_my_cpuload, 0);
	EXPECT_EQ(output->m_sampling_ratio, 0);
	EXPECT_EQ(output->m_n_tids_suppressed, 0);

	aggregator->stop();
	pool.stop_all();
	delete aggregator;
}

// make sure pushing two PBs aggregates them correctly
TEST(async_aggregator, multiple)
{
	test_helpers::scoped_config<unsigned int> config("aggregator.samples_between_flush", 2);

	blocking_queue<std::shared_ptr<flush_data_message>> input_queue(10);
	blocking_queue<std::shared_ptr<flush_data_message>> output_queue(10);

	dragent::async_aggregator* aggregator = new dragent::async_aggregator(input_queue,
																		  output_queue,
																		  // stupid short timeout because aint nobody got time for waiting for cleanup!
																		  1);
	dragent::watchdog_runnable_pool pool;
	pool.start(*aggregator, 1);
	std::atomic<bool> sent_metrics(false);

	draiosproto::metrics* input = new draiosproto::metrics();
	input->set_sampling_ratio(1);

	uint32_t timestamp = 1;
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields

	input = new draiosproto::metrics();
	input->set_sampling_ratio(2);
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp + 1,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields

	// sleep 
	for (uint32_t i = 0; output_queue.size() == 0 && i < 5000; ++i)
	{
		usleep(1000);
	}

	ASSERT_EQ(output_queue.size(), 1);
	std::shared_ptr<flush_data_message> output;
	bool ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_ts, timestamp + 1); // should get second timestamp
	EXPECT_EQ(output->m_metrics_sent, &sent_metrics);
	EXPECT_EQ(output->m_metrics->aggr_sampling_ratio().sum(), 3);

	// not applicable to aggregated output
	EXPECT_EQ(output->m_nevts, 0);
	EXPECT_EQ(output->m_num_drop_events, 0);
	EXPECT_EQ(output->m_my_cpuload, 0);
	EXPECT_EQ(output->m_sampling_ratio, 0);
	EXPECT_EQ(output->m_n_tids_suppressed, 0);

	aggregator->stop();
	pool.stop_all();
	delete aggregator;
	// input is deleted by the shared pointer that ends up wrapping it
}

// make sure the aggregator still works on the second aggregation after outputing (i.e.
// the output PB gets cleared
TEST(async_aggregator, followup_aggregation)
{
	test_helpers::scoped_config<unsigned int> config("aggregator.samples_between_flush", 1);

	blocking_queue<std::shared_ptr<flush_data_message>> input_queue(10);
	blocking_queue<std::shared_ptr<flush_data_message>> output_queue(10);

	dragent::async_aggregator* aggregator = new dragent::async_aggregator(input_queue,
																		  output_queue,
																		  // stupid short timeout because aint nobody got time for waiting for cleanup!
																		  1);
	dragent::watchdog_runnable_pool pool;
	pool.start(*aggregator, 1);
	std::atomic<bool> sent_metrics(false);

	draiosproto::metrics* input = new draiosproto::metrics();
	input->set_sampling_ratio(1);

	uint32_t timestamp = 1;
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields

	input = new draiosproto::metrics();
	input->set_sampling_ratio(2);
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp + 1,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields

	for (uint32_t i = 0; output_queue.size() != 2 && i < 5000; ++i)
	{
		usleep(1000);
	}

	ASSERT_EQ(output_queue.size(), 2);
	std::shared_ptr<flush_data_message> output;
	bool ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_ts, timestamp); // should get first timestamp
	EXPECT_EQ(output->m_metrics_sent, &sent_metrics);
	EXPECT_EQ(output->m_metrics->aggr_sampling_ratio().sum(), 1);

	ASSERT_EQ(output_queue.size(), 1);
	ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_ts, timestamp + 1); // should get second timestamp
	EXPECT_EQ(output->m_metrics_sent, &sent_metrics);
	EXPECT_EQ(output->m_metrics->aggr_sampling_ratio().sum(), 2);

	aggregator->stop();
	pool.stop_all();
	delete aggregator;
	// input is deleted by the shared pointer that ends up wrapping it
}

// make sure the limiter works
TEST(async_aggregator, limiter)
{
	test_helpers::scoped_config<uint32_t> config("aggregator.samples_between_flush", 1);
	test_helpers::scoped_config<uint32_t> config2("aggregator.container_limit", 5);

	blocking_queue<std::shared_ptr<flush_data_message>> input_queue(10);
	blocking_queue<std::shared_ptr<flush_data_message>> output_queue(10);

	dragent::async_aggregator* aggregator = new dragent::async_aggregator(input_queue,
																		  output_queue,
																		  // stupid short timeout because aint nobody got time for waiting for cleanup!
																		  1);
	dragent::watchdog_runnable_pool pool;
	pool.start(*aggregator, 10);
	std::atomic<bool> sent_metrics(false);

	draiosproto::metrics* input = new draiosproto::metrics();
	std::string machine_id = "zipperbox";
	input->set_machine_id(machine_id);
	for(int i = 0; i < 20; i++)
	{
		auto container = input->add_containers();
		container->set_id(std::to_string(i));
	}

	uint32_t timestamp = 1;
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields
	for (uint32_t i = 0; output_queue.size() == 0 && i < 5000; ++i)
	{
		usleep(1000);
	}

	ASSERT_EQ(output_queue.size(), 1);
	std::shared_ptr<flush_data_message> output;
	bool ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_metrics->containers().size(), 5);

	
	aggregator->stop();
	pool.stop_all();
	delete aggregator;
	// input is deleted by the shared pointer that ends up wrapping it
}

// make sure the post-aggregate substitutions are happening
TEST(async_aggregator, substitutions)
{
	test_helpers::scoped_config<uint32_t> config("aggregator.samples_between_flush", 1);

	blocking_queue<std::shared_ptr<flush_data_message>> input_queue(10);
	blocking_queue<std::shared_ptr<flush_data_message>> output_queue(10);

	dragent::async_aggregator* aggregator = new dragent::async_aggregator(input_queue,
																		  output_queue,
																		  // stupid short timeout because aint nobody got time for waiting for cleanup!
																		  1);
	dragent::watchdog_runnable_pool pool;
	pool.start(*aggregator, 10);
	std::atomic<bool> sent_metrics(false);

	draiosproto::metrics* input = new draiosproto::metrics();
	auto proc = input->add_programs();
	proc->mutable_procinfo()->mutable_details()->set_comm("wrong");
	proc->mutable_procinfo()->mutable_details()->set_exe("something just to make the hash different");
	proc->mutable_procinfo()->mutable_protos()->mutable_java()->set_process_name("right");
	uint64_t spid = 8675309;
	proc->add_pids(spid);
	uint64_t dpid = 1337;
	input->add_programs()->add_pids(dpid);
	auto conn = input->add_ipv4_connections();
	conn->set_spid(spid);
	conn->set_dpid(dpid);
	conn->set_state(draiosproto::connection_state::CONN_SUCCESS);
	auto iconn = input->add_ipv4_incomplete_connections_v2();
	iconn->set_spid(spid);
	iconn->set_dpid(dpid);
	iconn->set_state(draiosproto::connection_state::CONN_FAILED);

	uint32_t timestamp = 1;
	input_queue.put(std::make_shared<flush_data_message>(
		timestamp,
		&sent_metrics,
		*input,
		1,2,3,4,5
	)); // random numbers since we don't propagate those fields
	for (uint32_t i = 0; output_queue.size() == 0 && i < 5000; ++i)
	{
		usleep(1000);
	}

	ASSERT_EQ(output_queue.size(), 1);
	std::shared_ptr<flush_data_message> output;
	bool ret = output_queue.get(&output, 0);
	ASSERT_TRUE(ret);
	EXPECT_EQ(output->m_metrics->programs().size(), 2);
	EXPECT_EQ(output->m_metrics->programs()[0].procinfo().details().comm(), "right");
	EXPECT_EQ(output->m_metrics->programs()[0].pids().size(), 1);
	EXPECT_EQ(output->m_metrics->programs()[0].pids()[0],
			  metrics_message_aggregator_impl::program_java_hasher(output->m_metrics->programs()[0]));
	EXPECT_EQ(output->m_metrics->programs()[1].pids()[0],
			  metrics_message_aggregator_impl::program_java_hasher(output->m_metrics->programs()[1]));
	EXPECT_EQ(output->m_metrics->ipv4_connections().size(), 1);
	EXPECT_EQ(output->m_metrics->ipv4_connections()[0].spid(),
			  output->m_metrics->programs()[0].pids()[0]);
	EXPECT_EQ(output->m_metrics->ipv4_connections()[0].dpid(),
			  output->m_metrics->programs()[1].pids()[0]);
	EXPECT_EQ(output->m_metrics->ipv4_incomplete_connections_v2().size(), 1);
	EXPECT_EQ(output->m_metrics->ipv4_incomplete_connections_v2()[0].spid(),
			  output->m_metrics->programs()[0].pids()[0]);
	EXPECT_EQ(output->m_metrics->ipv4_incomplete_connections_v2()[0].dpid(),
			  output->m_metrics->programs()[1].pids()[0]);
	
	aggregator->stop();
	pool.stop_all();
	delete aggregator;
	// input is deleted by the shared pointer that ends up wrapping it
}
