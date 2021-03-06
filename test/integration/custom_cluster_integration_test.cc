#include "envoy/api/v2/eds.pb.h"

#include "common/network/address_impl.h"
#include "common/upstream/load_balancer_impl.h"

#include "test/config/utility.h"
#include "test/integration/clusters/cluster_factory_config.pb.h"
#include "test/integration/clusters/custom_static_cluster.h"
#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

const int UpstreamIndex = 0;

// Integration test for cluster extension using CustomStaticCluster.
class CustomClusterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  CustomClusterIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initialize() override {
    setUpstreamCount(1);
    // change the configuration of the cluster_0 to a custom static cluster
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);

      cluster_0->clear_hosts();

      envoy::api::v2::Cluster_CustomClusterType cluster_type;
      cluster_type.set_name("envoy.clusters.custom_static");
      test::integration::clusters::CustomStaticConfig config;
      config.set_priority(10);
      config.set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      config.set_port_value(fake_upstreams_[UpstreamIndex]->localAddress()->ip()->port());
      cluster_type.mutable_typed_config()->PackFrom(config);

      cluster_0->mutable_cluster_type()->CopyFrom(cluster_type);
    });
    HttpIntegrationTest::initialize();
    test_server_->waitForGaugeGe("cluster_manager.active_clusters", 1);
  }

  Network::Address::IpVersion ipVersion() const { return version_; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CustomClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(CustomClusterIntegrationTest, TestRouterHeaderOnly) {
  testRouterHeaderOnlyRequestAndResponse(nullptr, UpstreamIndex);
}

TEST_P(CustomClusterIntegrationTest, TestTwoRequests) { testTwoRequests(false); }

TEST_P(CustomClusterIntegrationTest, TestCustomConfig) {
  // Calls our initialize(), which includes establishing a listener, route, and cluster.
  initialize();

  // Verify the cluster is correctly setup with the custom priority
  const auto& cluster_map = test_server_->server().clusterManager().clusters();
  EXPECT_EQ(1, cluster_map.size());
  EXPECT_EQ(1, cluster_map.count("cluster_0"));
  const auto& cluster_ref = cluster_map.find("cluster_0")->second;
  const auto& hostset_per_priority = cluster_ref.get().prioritySet().hostSetsPerPriority();
  EXPECT_EQ(11, hostset_per_priority.size());
  const Envoy::Upstream::HostSetPtr& host_set = hostset_per_priority[10];
  EXPECT_EQ(1, host_set->hosts().size());
  EXPECT_EQ(1, host_set->healthyHosts().size());
  EXPECT_EQ(10, host_set->priority());
}
} // namespace
} // namespace Envoy
