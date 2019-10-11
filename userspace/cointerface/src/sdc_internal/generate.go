// Assumes that cmake will set the env vars
//go:generate sh -c "protoc -I ${PROTO_SRC_DIR} --gofast_out=plugins=grpc,Mdraios.proto=github.com/draios/protorepo/draiosproto:. ${PROTO_SRC_DIR}/sdc_internal.proto"

package sdc_internal