import json
import logging
import urllib2
import socket

from urlparse import urlparse

from enum import Enum

from utils.network import Network


class SolrMetrics(object):

    class Tag(Enum):
        COLLECTION = 1
        SHARD = 2
        CORE = 3
        CORE_ALIAS = 4

    TAG_NAME = {
        Tag.COLLECTION: "solr.tag.collection:%s",
        Tag.SHARD: "solr.tag.shard:%s",
        Tag.CORE: "solr.tag.core:%s",
        Tag.CORE_ALIAS: "solr.tag.core_alias:%s",
    }

    class METRIC_NAME_ENUM(Enum):
        LIVE_NODES = 1,
        SHARDS = 2,
        REPLICA = 3,
        DOCUMENT_COUNT = 4,
        BROWSE_RPS = 5,
        SELECT_RPS = 6,
        GET_RPS = 7,
        QUERY_RPS = 8,
        UPDATE_RPS = 9,
        INDEX_SIZE = 10,
        BROWSE_RT = 11,
        SELECT_RT = 12,
        GET_RT = 13,
        QUERY_RT = 14,
        UPDATE_RT = 15,
        HOST_SHARD_COUNT = 16
        COLLECTION_SHARD_COUNT = 17
        DOCUMENT_COUNT_PER_COLLECTION = 18
        NONE = 100

    class Endpoint(Enum):
        LIVE_NODES = 1
        SHARDS = 2
        REPLICA = 3
        DOCUMENT_COUNT = 4
        COLLECTION = 5
        NODE = 6
        CORES_INFO = 7
        VERSION = 8
        STATS = 9
        COLLECTION_DOCUMENT_COUNT = 11

    URL = {
        Endpoint.LIVE_NODES: "/solr/admin/collections?action=clusterstatus&wt=json",
        Endpoint.SHARDS: "/solr/admin/collections?action=clusterstatus&wt=json",
        Endpoint.REPLICA: "/solr/admin/collections?action=clusterstatus&wt=json",
        Endpoint.DOCUMENT_COUNT: "/solr/admin/cores?wt=json",
        Endpoint.COLLECTION: "/solr/admin/collections?action=clusterstatus&wt=json",
        Endpoint.COLLECTION_DOCUMENT_COUNT: "/solr/%s/select?indent=on&q=*:*&rows=0&start=0&wt=json"
    }

    class Metric:

        class MetricType(Enum):
            gauge = 1
            counter = 2
            rate = 3

        def __init__(self, name, value, tags, metricType = MetricType.gauge):
            self.name = name
            self.value = value
            self.tags = tags
            self.metricType = metricType


        def getValue(self):
            return self.value

        def getTags(self):
            return self.tags

        def getName(self):
            return self.name

        def getType(self):
            return self.metricType

        def __repr__(self):
            return ("(name:{}, value: {}. tags: {})").format(self.name, self.value, self.tags)

        def __str__(self):
            return ("(name:{}, value: {}. tags: {})").format(self.name, self.value, self.tags)

    class Core:
        def __init__(self, name, alias, shard, collection, base_url, port, leader = None):
            self.name = name
            self.alias = alias
            self.shard = shard
            self.collection = collection
            self.base_url = base_url
            self.port = port
            self.leader = leader

        def __hash__(self):
            return ("{}{}{}{}{}").format(self.name, self.alias, self.shard, self.collection, self.base_url).__hash__()

        def __eq__(self, other):
            return self.name == other.name and self.alias == other.alias and self.shard == other.shard and self.collection == other.collection and self.base_url == other.base_url

        def getPort(self):
            return self.port


    def __init__(self, version, instance):
        self.version = version
        self.instance = instance
        self.ports = instance["ports"]
        self.port = 0
        self.host = instance["host"]
        self.network = Network()
        self.localCores = set()
        self.localLeaderCores = set()
        self.localEndpoints = set()
        self.collectionByCore = dict()
        self.log = logging.getLogger(__name__)

    def check(self):
        # This should be run just once in a while
        self._retrieveLocalEndpointsAndCores()
        self.log.debug(str("start checking solr host {} , ports:").format(self.host, self.ports))
        ret = [
            self._getLiveNodes(),
            self._getReplica(),
            self._getLocalDocumentCount(),
            self._getAllRpsAndRequestTime(),
            self._getIndexSize(),
            self._getCollectionShardCount(),
            self._getHostShardCount(),
        ]
        return ret

    def getMajorNumberVersion(self):
        return int(self.version[0:1])

    @staticmethod
    def formatUrl(host, port, handler):
        ret = "http://" + host + ":" + str(port) + handler
        return ret

    @staticmethod
    def getUrl(host, ports, handler):
        found = False
        foundPort = 0
        for port in ports:
            try:
                if found is True:
                    break
                url = SolrMetrics.formatUrl(host, port, handler)
                data = urllib2.urlopen(url)
                obj = json.load(data)
                found = True
                foundPort = port
            except:
                found = False
        if found is True:
            return [obj, foundPort]
        else:
            return [{}, 0]

    def _getUrl(self, handler):
        obj, self.port = SolrMetrics.getUrl(self.host, self.ports, handler)
        return obj

    def _getUrlWithBase(self, baseUrl, handler):
        url = str("{}{}").format(baseUrl[0:baseUrl.find('/solr')], handler)
        try:
            data = urllib2.urlopen(url)
            obj = json.load(data)
        except:
            return {}

        return obj

    def _getLiveNodesEndpoint(self):
        ret = []
        obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.LIVE_NODES])

        if len(obj) > 0:
            try:
                for node in obj["cluster"]["live_nodes"]:
                    ret.append(node)
            except KeyError:
                pass
        return ret

    def _getLiveNodes(self):
        ret = []
        obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.LIVE_NODES])

        if len(obj) > 0:
            try:
                live_nodes = len(obj["cluster"]["live_nodes"])
                self.log.debug(("detected {} live nodes").format(live_nodes))
                tags = [
                    self.TAG_NAME[self.Tag.COLLECTION][0:-2],  # set
                ]
                ret.append(self.Metric(self.METRIC_NAME_ENUM.LIVE_NODES, live_nodes, tags))
            except KeyError:
                pass
        return ret

    def _getCollectionShardCount(self):
        ret = []
        try:
            obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.SHARDS])
            if len(obj) == 0:
                return ret

            for collection in obj["cluster"]["collections"]:
                shards_per_collection = len(obj["cluster"]["collections"][collection]["shards"])
                tags = [ self.TAG_NAME[self.Tag.COLLECTION] % collection ]
                ret.append(self.Metric(self.METRIC_NAME_ENUM.COLLECTION_SHARD_COUNT, shards_per_collection, tags))
        except Exception as e:
            self.log.error(("Got Error while fetching collection shard count: {}").format(e))
        return ret

    def _getHostShardCount(self):
        ret = []
        try:
            obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.SHARDS])
            if len(obj) == 0:
                return ret

            for collection in obj["cluster"]["collections"]:
                shards_per_host = 0
                shards = obj["cluster"]["collections"][collection]["shards"]

                for shard in shards.values():
                    for replica in shard["replicas"].values():
                        base_url = replica["base_url"]
                        node_name = urlparse(base_url).hostname
                        node_ip_address = socket.gethostbyname(node_name)
                        if self.network.ipIsLocalHostOrDockerContainer(node_ip_address):
                            # found a replica that is local to this host
                            shards_per_host = shards_per_host + 1
                            break

                tags = [ self.TAG_NAME[self.Tag.COLLECTION] % collection ]
                ret.append(self.Metric(self.METRIC_NAME_ENUM.HOST_SHARD_COUNT, shards_per_host, tags))
        except Exception as e:
            self.log.error(("Got Error while fetching host shard count: {}").format(e))
        return ret

    def _getReplica(self):
        class replicaPerNode:
            pass

        ret = []
        try:
            obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.REPLICA])
            if len(obj) > 0:
                for collectionName, collection in obj["cluster"]["collections"].iteritems():
                    for shardName, shard in collection["shards"].iteritems():
                        replicaPerNodeMap = {}
                        # replicaName is an internal Solr representation
                        for replicaName, replica in shard["replicas"].iteritems():
                            if replica["state"] == "active":
                                nodeName = replica["node_name"]
                                coreName = replica["core"]
                                baseUrl = replica["base_url"]
                                thisCore = self.Core(coreName, replicaName, shardName, collectionName, baseUrl, urlparse(baseUrl).port)
                                if thisCore in self.localCores:
                                    if replicaPerNodeMap.has_key(nodeName):
                                        replicaPerNodeMap[nodeName].len = replicaPerNodeMap[nodeName].len + 1
                                    else:
                                        newEntry = replicaPerNode()
                                        newEntry.len = 1
                                        newEntry.name = nodeName
                                        newEntry.collection = collectionName
                                        newEntry.shard = shardName
                                        replicaPerNodeMap[nodeName] = newEntry
                                else:
                                    self.log.debug(str("skipping core {}.{} because it is not local").format(replicaName, coreName))
                        for nodeName in replicaPerNodeMap:
                            tags = [
                                self.TAG_NAME[self.Tag.COLLECTION] % collectionName,
                            ]
                            ret.append(self.Metric(self.METRIC_NAME_ENUM.REPLICA, replicaPerNodeMap[nodeName].len, tags))
                            self.log.debug(("detected {} replica with tags {}").format(replicaPerNodeMap[nodeName].len, tags))
        except Exception as e:
            self.log.error(("Got Error while fetching replica: {}").format(e))

        return ret

    def _getLocalDocumentCount(self):
        ret = []
        for base in self.localEndpoints:
            mets = self._getCoreDocumentCount(base)
            ret.extend(mets)
        return ret

    def _getCoreDocumentCount(self, base):
        ret = []
        try:
            obj = self._getUrlWithBase(base, SolrMetrics.URL[SolrMetrics.Endpoint.DOCUMENT_COUNT])
            if len(obj) > 0:
                for replica_alias in obj["status"]:
                    if replica_alias not in self.localLeaderCores:
                        continue
                    collection = self.collectionByCore.get(replica_alias, None)
                    # collection = obj["status"][replica_alias]["cloud"]["collection"]
                    # shard = obj["status"][replica_alias]["cloud"]["shard"]
                    # replica = obj["status"][replica_alias]["cloud"]["replica"]
                    numDocs = obj["status"][replica_alias]["index"]["numDocs"]
                    tags = [
                        self.TAG_NAME[self.Tag.CORE] % replica_alias
                    ]
                    if collection is not None:
                        tags.append(self.TAG_NAME[self.Tag.COLLECTION] % collection)
                    ret.append(self.Metric(self.METRIC_NAME_ENUM.DOCUMENT_COUNT, numDocs, tags))
        except Exception as e:
            self.log.error(("Got Error while fetching core document count: {}").format(e))
        return ret

    def _getDocumentCount(self):
        ret = []
        try:
            obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.DOCUMENT_COUNT])
            if len(obj) > 0:
                for replica_alias in obj["status"]:
                    collection = obj["status"][replica_alias]["cloud"]["collection"]
                    shard = obj["status"][replica_alias]["cloud"]["shard"]
                    replica = obj["status"][replica_alias]["cloud"]["replica"]
                    numDocs = obj["status"][replica_alias]["index"]["numDocs"]
                    tags = [
                        self.TAG_NAME[self.Tag.COLLECTION] % collection,
                        self.TAG_NAME[self.Tag.SHARD] % shard,
                        self.TAG_NAME[self.Tag.CORE] % replica_alias,
                    ]
                    ret.append(self.Metric(self.METRIC_NAME_ENUM.DOCUMENT_COUNT, numDocs, tags))
        except Exception as e:
            self.log.error(("Got Error while fetching documetn count: {}").format(e))
        return ret

    def _retrieveLocalEndpointsAndCores(self):
        obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.LIVE_NODES])
        if len(obj) > 0:
            try:
                for collectionName, collection in obj["cluster"]["collections"].iteritems():
                    for shardName, shard in collection["shards"].iteritems():
                        for replicaName, replica in shard["replicas"].iteritems():
                            base_url = replica["base_url"]
                            parsedUrl = urlparse(base_url)
                            hostname_from_url = parsedUrl.hostname
                            port_from_url = parsedUrl.port
                            ip_address = socket.gethostbyname(hostname_from_url)
                            if self.network.ipIsLocalHostOrDockerContainer(ip_address):
                                coreName = replica["core"]
                                leader = replica.get("leader", False)
                                if bool(leader):
                                    self.localLeaderCores.add(coreAlias)
                                self.collectionByCore[coreAlias] = collection
                                self.localCores.add(self.Core(coreName, replicaName, shardName, collectionName, base_url, port_from_url, leader))
                                self.localEndpoints.add(replica["base_url"])
            except Exception as e:
                self.log.error(("Got Error while fetching local core: {}").format(e))

        self.log.debug(str("detected {} local cores: {}").format(len(self.localCores), self.localCores))
        self.log.debug(str("detected {} local nodes: {}").format(len(self.localEndpoints), self.localEndpoints))

    def _getCollections(self):
        ret = []
        obj = self._getUrl(SolrMetrics.URL[SolrMetrics.Endpoint.COLLECTION])
        if len(obj) > 0:
            for collection in obj["cluster"]["collections"]:
                ret.append(collection)
        return ret

