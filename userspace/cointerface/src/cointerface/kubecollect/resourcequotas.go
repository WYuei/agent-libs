package kubecollect

import (
	"cointerface/draiosproto"
	"context"
	log "github.com/cihub/seelog"
	"github.com/gogo/protobuf/proto"
	"k8s.io/api/core/v1"
	v1meta "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/fields"
	"k8s.io/apimachinery/pkg/labels"
	kubeclient "k8s.io/client-go/kubernetes"
	"k8s.io/client-go/tools/cache"
	"sync"
)

var resourceQuotaInf cache.SharedInformer

// make this a library function?
func resourceQuotaEvent(rq *v1.ResourceQuota, eventType *draiosproto.CongroupEventType) (draiosproto.CongroupUpdateEvent) {
	return draiosproto.CongroupUpdateEvent {
		Type: eventType,
		Object: newResourceQuotaCongroup(rq),
	}
}

func setScope(tags map[string]string, resourcequota *v1.ResourceQuota) () {
	scopes := resourcequota.Spec.Scopes;
	var terminatingTag = "kubernetes.resourcequota.scope.terminating";
	var nonterminatingTag = "kubernetes.resourcequota.scope.notterminating"
	var besteffortTag = "kubernetes.resourcequota.scope.besteffort"
	var nonbesteffortTag = "kubernetes.resourcequota.scope.notbesteffort"

	tags[terminatingTag] = "false"
	tags[nonterminatingTag] = "false"
	tags[besteffortTag] = "false"
	tags[nonbesteffortTag] = "false"

	for i :=0; i<len(scopes); i++ {
		if(scopes[i]=="Terminating"){
			tags[terminatingTag] = "true"
		} else if(scopes[i] == "NotTerminating"){
			tags[nonterminatingTag] = "true"
		} else if(scopes[i]=="BestEffort"){
			tags[besteffortTag] = "true"
		} else if(scopes[i]=="NotBestEffort"){
			tags[nonbesteffortTag] = "true"
		}
	}
}

func resourceQuotaEquals(oldResourceQuota *v1.ResourceQuota, newResourceQuota *v1.ResourceQuota) bool {
	ret := true;
	if oldResourceQuota.GetName() != newResourceQuota.GetName() ||
		!EqualLabels(oldResourceQuota.ObjectMeta, newResourceQuota.ObjectMeta) ||
		!EqualAnnotations(oldResourceQuota.ObjectMeta, newResourceQuota.ObjectMeta) ||
		!equalResourceList(oldResourceQuota.Status.Used, newResourceQuota.Status.Used){
		ret = false
	}
	return ret
}

func newResourceQuotaCongroup(resourceQuota *v1.ResourceQuota) (*draiosproto.ContainerGroup) {
	// Need a way to distinguish them
	// ... and make merging annotations+labels it a library function?
	//     should work on all v1.Object types
	tags := make(map[string]string)
	for k, v := range resourceQuota.GetLabels() {
		tags["kubernetes.resourcequota.label." + k] = v
	}

	setScope(tags, resourceQuota)
	tags["kubernetes.resourcequota.name"] = resourceQuota.GetName()

	ret := &draiosproto.ContainerGroup{
		Uid: &draiosproto.CongroupUid{
			Kind:proto.String("k8s_resourcequota"),
			Id:proto.String(string(resourceQuota.GetUID()))},
		Tags: tags,
	}

	AddResourceQuotaMetrics(&ret.Metrics, resourceQuota)
	AddNSParents(&ret.Parents, resourceQuota.GetNamespace())
	AddPodChildren(&ret.Children, labels.NewSelector(),resourceQuota.GetNamespace())
	return ret
}

func AddResourceQuotaMetrics(metrics *[]*draiosproto.AppMetric, resourceQuota *v1.ResourceQuota) {
	prefix := "kubernetes.resourcequota."

	for k, v := range resourceQuota.Status.Used {
		hard := resourceQuota.Status.Hard[k]

		// Take MilliValue() and divide because
		// we could lose precision with Value()
		AppendMetric(metrics, prefix+k.String()+".hard", float64(hard.MilliValue())/1000);
		AppendMetric(metrics, prefix+k.String()+".used", float64(v.MilliValue())/1000);
	}
}

func AddResourceQuotaParentsFromPod(parents *[]*draiosproto.CongroupUid, pod *v1.Pod) {
	if !resourceReady("resourcequotas") {
		return
	}

	for _, obj := range resourceQuotaInf.GetStore().List() {
		resourcequota := obj.(*v1.ResourceQuota)
		if(resourcequota.GetNamespace() == pod.GetNamespace()) {
			*parents = append(*parents, &draiosproto.CongroupUid{
				Kind:proto.String("k8s_resourcequota"),
				Id:proto.String(string(resourcequota.GetUID()))})
		}
	}
}

func AddResourceQuotaChildrenFromNamespace(children *[]*draiosproto.CongroupUid, namespaceName string) {
	if !resourceReady("resourcequotas") {
		return
	}

	for _, obj := range resourceQuotaInf.GetStore().List() {
		resourceQuota := obj.(*v1.ResourceQuota)
		if resourceQuota.GetNamespace() == namespaceName {
			*children = append(*children, &draiosproto.CongroupUid{
				Kind:proto.String("k8s_resourcequota"),
				Id:proto.String(string(resourceQuota.GetUID()))})
		}
	}
}

func startResourceQuotasSInformer(ctx context.Context, kubeClient kubeclient.Interface, wg *sync.WaitGroup, evtc chan<- draiosproto.CongroupUpdateEvent) {
	client := kubeClient.CoreV1().RESTClient()
	lw := cache.NewListWatchFromClient(client, "ResourceQuotas", v1meta.NamespaceAll, fields.Everything())
	resourceQuotaInf = cache.NewSharedInformer(lw, &v1.ResourceQuota{}, RsyncInterval)

	wg.Add(1)
	go func() {
		watchResourceQuotas(evtc)
		resourceQuotaInf.Run(ctx.Done())
		wg.Done()
	}()
}

func watchResourceQuotas(evtc chan<- draiosproto.CongroupUpdateEvent) {
	log.Debugf("In WatchResourceQuotas()")

	resourceQuotaInf.AddEventHandler(
		cache.ResourceEventHandlerFuncs{
			AddFunc: func(obj interface{}) {
				eventReceived("resourcequotas")
				evtc <- resourceQuotaEvent(obj.(*v1.ResourceQuota),
					draiosproto.CongroupEventType_ADDED.Enum())
				addEvent("ResourceQuota", EVENT_ADD)
			},
			UpdateFunc: func(oldObj, newObj interface{}) {
				oldResourceQuota := oldObj.(*v1.ResourceQuota)
				newResourceQuota := newObj.(*v1.ResourceQuota)
				if (oldResourceQuota.GetResourceVersion() != newResourceQuota.GetResourceVersion()) ||
					!resourceQuotaEquals(oldResourceQuota, newResourceQuota) {

					evtc <- resourceQuotaEvent(newResourceQuota,
						draiosproto.CongroupEventType_UPDATED.Enum())
					addEvent("ResourceQuota", EVENT_UPDATE_AND_SEND)
				}
				addEvent("ResourceQuota", EVENT_UPDATE)
			},
			DeleteFunc: func(obj interface{}) {
				oldRQ := (*v1.ResourceQuota)(nil)
				switch obj.(type) {
				case *v1.ResourceQuota:
					oldRQ = obj.(*v1.ResourceQuota)
				case cache.DeletedFinalStateUnknown:
					d := obj.(cache.DeletedFinalStateUnknown)
					o, ok := (d.Obj).(*v1.ResourceQuota)
					if ok {
						oldRQ = o
					} else {
						log.Warn("DeletedFinalStateUnknown without resourcequota object")
					}
				default:
					log.Warn("Unknown object type in resourcequota DeleteFunc")
				}
				if oldRQ == nil {
					return
				}


				evtc <- resourceQuotaEvent(oldRQ,
					draiosproto.CongroupEventType_REMOVED.Enum())
				addEvent("ResourceQuota", EVENT_DELETE)
			},
		},
	)
}
