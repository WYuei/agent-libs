package kubecollect_common

import (
	"context"
	"math/rand"
	"testing"
	"time"

	log "github.com/cihub/seelog"
	"github.com/draios/protorepo/sdc_internal"
	"github.com/gogo/protobuf/proto"
)

func TestMapInsert(t *testing.T) {
	var m map[string]string

	MapInsert(&m, "good", "bye")

	if m["good"] != "bye" {
		t.Fail()
	}
}

func TestGetBackoffValue_invalidPreviousBackoff(t *testing.T) {

	value := getBackoffValue(3*time.Second, 0*time.Second)
	expected := WATCHER_MINIMUM_BACKOFF

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}
}

func TestGetBackoffValue_normalComplete(t *testing.T) {

	value := getBackoffValue(3*time.Second, 0*time.Second)
	// Don't use contants for this test; manually check values
	expected := 1 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 2 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 4 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 8 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 16 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 32 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 1 * time.Hour

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 1 * time.Hour

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 1 * time.Hour

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 1 * time.Hour

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}
}

func TestGetBackoffValue_normalRecover(t *testing.T) {

	value := getBackoffValue(3*time.Second, 0*time.Second)
	// Don't use contants for this test; manually check values
	expected := 1 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 2 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, expected)
	expected = 4 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(1*time.Hour+time.Second, expected)
	expected = 1 * time.Minute

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}
}

func TestGetBackoffValue_maxBackoff(t *testing.T) {

	value := getBackoffValue(3*time.Second, WATCHER_MAXIMUM_BACKOFF-time.Second)
	expected := WATCHER_MAXIMUM_BACKOFF

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, WATCHER_MAXIMUM_BACKOFF)
	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}

	value = getBackoffValue(3*time.Second, WATCHER_MAXIMUM_BACKOFF+time.Second)
	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}
}

func TestGetBackoffValue_longRuntime(t *testing.T) {

	value := getBackoffValue(3*time.Hour, 1*time.Hour)
	expected := WATCHER_MINIMUM_BACKOFF

	if value != expected {
		t.Errorf("%s != %s", value.String(), expected.String())
		t.Fail()
	}
}

func TestGetBackoffValue_random(t *testing.T) {

	value := getBackoff(3*time.Hour, 1*time.Hour)
	min := WATCHER_MINIMUM_BACKOFF
	max := min + min/2

	if value < min || value > max {
		t.Errorf("%s < %s || %s > %s", value.String(), min.String(), value.String(), max.String())
		t.Fail()
	}

	value = getBackoff(3*time.Second, 2*time.Minute)
	min = 4 * time.Minute
	max = min + min/2

	if value < min || value > max {
		t.Errorf("%s < %s || %s > %s", value.String(), min.String(), value.String(), max.String())
		t.Fail()
	}

	value = getBackoff(3*time.Second, WATCHER_MAXIMUM_BACKOFF)
	min = WATCHER_MAXIMUM_BACKOFF
	max = min + min/2

	if value < min || value > max {
		t.Errorf("%s < %s || %s > %s", value.String(), min.String(), value.String(), max.String())
		t.Fail()
	}
}

func TestColdStartClient(t *testing.T) {
	cmd := &sdc_internal.OrchestratorEventsStreamCommand{
		Url:                       proto.String("http://localhost:8081"),
		CaCert:                    proto.String(""),
		ClientCert:                proto.String(""),
		ClientKey:                 proto.String(""),
		QueueLen:                  proto.Uint32(0),
		StartupGc:                 proto.Int32(0),
		StartupInfWaitTimeS:       proto.Uint32(0),
		StartupTickIntervalMs:     proto.Uint32(0),
		StartupLowTicksNeeded:     proto.Uint32(0),
		StartupLowEvtThreshold:    proto.Uint32(0),
		FilterEmpty:               proto.Bool(false),
		SslVerifyCertificate:      proto.Bool(false),
		AuthToken:                 proto.String(""),
		AnnotationFilter:          make([]string, 0),
		IncludeTypes:              make([]string, 0),
		EventCountsLogTime:        proto.Uint32(0),
		BatchMsgsQueueLen:         proto.Uint32(0),
		BatchMsgsTickIntervalMs:   proto.Uint32(0),
		MaxRndConnDelay:           proto.Uint32(0),
		PodStatusAllowlist:        make([]string, 0),
		ThinCointerface:           proto.Bool(false),
		PodPrefixForCidrRetrieval: make([]string, 0),
		TerminatedPodsEnabled:     proto.Bool(false),
		ColdStartNum:              proto.Uint32(4),
		MaxWaitForLock:            proto.Uint32(0),
		MaxColdStartDuration:      proto.Uint32(0),
		EnforceLeaderElection:     proto.Bool(false),
		CointerfaceDelegation:     proto.Bool(false),
	}

	ctx, _ := context.WithCancel(context.Background())
	client, _, err := createLeasePoolClient(context.Background(), "/tmp/goodDayTest.sock", "goodDayTest", *cmd.ColdStartNum, cmd)

	if err != nil {
		t.Logf("Failed Creating cold start client: %s", err.Error())
		return
	}

	wait, err := (*client).WaitLease(ctx, &sdc_internal.LeasePoolNull{})

	if err != nil {
		t.Error(err.Error())
		return
	}

	res, err := wait.Recv()
	if err != nil {
		t.Logf(err.Error())
		log.Flush()
		return
	}

	if *res.Successful {
		t.Log("Hooray!!!")
		log.Flush()
	} else {
		t.Log("Could not get the lock :-(")
	}
}

func TestCalculaterandomDelay(t *testing.T) {
	type Case struct {
		perNodeConnDelay   float64
		nodes              uint32
		minRandomConnDelay uint32
		maxRandomConnDelay uint32
		expectedUpperLimit float64
		checkExpected      bool
	}
	cases := []Case{
		{0, 0, 1, 2, 1, true},
		{0, 0, 0, 0, 0, true},
		{0.1, 1, 0, 5, 0.1, true},
		{0.1, 2, 0, 5, 0.2, true},
		{0.1, 1, 1, 5, 1, true},
		{0.1, 1000, 1, 5, 5, true},
	}

	// Let's ensure that random values does not create issues.
	for i := 0; i < 1000; i++ {
		cases = append(cases, Case{rand.Float64(), rand.Uint32(), rand.Uint32(), rand.Uint32(), 0, false})
	}

	for k, ut := range cases {
		actualDelay := calculateRandomDelay(ut.perNodeConnDelay, ut.nodes, ut.minRandomConnDelay, ut.maxRandomConnDelay)

		if ut.checkExpected == true && actualDelay > ut.expectedUpperLimit {
			t.Logf("Use case %d(%v) failed. Expected Max %f. Actual: %f", k, ut, ut.expectedUpperLimit, actualDelay)
			t.Fail()
		}

		if actualDelay > float64(ut.maxRandomConnDelay) {
			t.Logf("Use case %d(%v) failed. delay is greater then maxRandomConnDelay: %f,%d", k, ut, actualDelay, ut.maxRandomConnDelay)
			t.Fail()
		}

		if actualDelay < float64(ut.minRandomConnDelay) && (ut.minRandomConnDelay < ut.maxRandomConnDelay) {
			t.Logf("Use case %d(%v) failed. delay is smaller then minRandomConnDelay: %f,%d", k, ut, actualDelay, ut.minRandomConnDelay)
			t.Fail()
		}
	}

}
