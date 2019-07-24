#include <a0/pubsub.h>

#include <stdlib.h>
#include <string.h>

static const uint8_t A0_PUBSUB_PROTOCOL_NAME[] = "a0_pubsub_v0.0.0";

errno_t a0_topic_path(a0_topic_t* topic, char** path, size_t* path_size) {
  // TODO: assert topic->name is entirely isalnum + '_' and does not contain '__'.
  // TODO: assert topic->as_container is entirely isalnum + '_' and does not contain '__'.

  if (!topic->as_container.size) {
    const char* container_name = getenv("A0_CONTAINER_NAME");
    topic->as_container.size = strlen(container_name);
    topic->as_container.ptr = malloc(topic->as_container.size);
    strcpy((char*)topic->as_container.ptr, container_name);
  }

  FILE* ss = open_memstream(path, path_size);
  fprintf(ss, "/%.*s__%.*s__%.*s",
          (int)sizeof(A0_PUBSUB_PROTOCOL_NAME), A0_PUBSUB_PROTOCOL_NAME,
          (int)topic->as_container.size, topic->as_container.ptr,
          (int)topic->name.size, topic->name.ptr);
  fflush(ss);
  fclose(ss);

  return A0_OK;
}

errno_t a0_topic_shmobj(a0_topic_t* topic, const a0_shmobj_options_t* shmobj_opts, a0_shmobj_t* shmobj) {
  char* path;
  size_t path_size;
  _A0_RETURN_ERR_ON_ERR(a0_topic_path(topic, &path, &path_size));

  errno_t err = a0_shmobj_create_or_attach(path, shmobj_opts, shmobj);
  free(path);
  return err;
}

void _a0_publisher_stream_construct(a0_stream_t* stream) {
  a0_locked_stream_t lk;
  a0_lock_stream(&lk, stream);

  a0_buf_t protocol_metadata;
  a0_stream_protocol_metadata(&lk, &protocol_metadata);
  memcpy(protocol_metadata.ptr, A0_PUBSUB_PROTOCOL_NAME, sizeof(A0_PUBSUB_PROTOCOL_NAME));
  
  a0_unlock_stream(&lk);
}

void _a0_publisher_stream_already_constructed(a0_stream_t* stream) {
  // TODO: Assert protocol_metadata == A0_PUBSUB_PROTOCOL_NAME.
}

errno_t a0_publisher_init(a0_publisher_t* publisher) {
  if (!publisher->shmobj_options.size) {
    publisher->shmobj_options.size = 32 * 1024 * 1024;  // Default to 32MB.
  }

  _A0_RETURN_ERR_ON_ERR(a0_topic_shmobj(&publisher->topic, &publisher->shmobj_options, &publisher->_shmobj));

  publisher->_stream_construct_options.protocol_metadata_size = sizeof(A0_PUBSUB_PROTOCOL_NAME);
  publisher->_stream_construct_options.on_construct = _a0_publisher_stream_construct;
  publisher->_stream_construct_options.on_already_constructed = _a0_publisher_stream_already_constructed;

  publisher->_stream_options.shmobj = &publisher->_shmobj;
  publisher->_stream_options.construct_opts = &publisher->_stream_construct_options;

  _A0_RETURN_ERR_ON_ERR(a0_stream_init(&publisher->_stream, &publisher->_stream_options));

  return A0_OK;
}

errno_t a0_publisher_close(a0_publisher_t* publisher) {
  a0_stream_close(&publisher->_stream);
  a0_shmobj_detach(&publisher->_shmobj);
  return A0_OK;
}

size_t a0_packet_serial_size(a0_packet_t* pkt) {
  size_t size = 0;
  size += sizeof(size_t);  // Number of header fields;
  for (size_t i = 0; i < pkt->header.num_fields; i++) {
    size += sizeof(size_t);  // Field key size.
    size += pkt->header.fields[i].key.size;
    size += sizeof(size_t);  // Field value size. 
    size += pkt->header.fields[i].value.size;
  }
  size += sizeof(size_t);  // Payload size;
  size += pkt->payload.size;
  return size;
}

void a0_packet_serialize(a0_packet_t* pkt, a0_buf_t* space) {
  uint8_t* addr = space->ptr;

  memcpy(addr, &pkt->header.num_fields, sizeof(size_t));
  addr += sizeof(size_t);

  for (size_t i = 0; i < pkt->header.num_fields; i++) {
    memcpy(addr, &pkt->header.fields[i].key.size, sizeof(size_t));
    addr += sizeof(size_t);

    memcpy(addr, pkt->header.fields[i].key.ptr, pkt->header.fields[i].key.size);
    addr += pkt->header.fields[i].key.size;

    memcpy(addr, &pkt->header.fields[i].value.size, sizeof(size_t));
    addr += sizeof(size_t);

    memcpy(addr, pkt->header.fields[i].value.ptr, pkt->header.fields[i].value.size);
    addr += pkt->header.fields[i].value.size;
  }

  memcpy(addr, &pkt->payload.size, sizeof(size_t));
  addr += sizeof(size_t);

  memcpy(addr, pkt->payload.ptr, pkt->payload.size);
}

errno_t a0_pub(a0_publisher_t* publisher, a0_packet_t* pkt) {
  size_t serial_size = a0_packet_serial_size(pkt);

  a0_locked_stream_t lk;
  _A0_RETURN_ERR_ON_ERR(a0_lock_stream(&lk, &publisher->_stream));

  errno_t err = A0_OK;

  a0_buf_t write_space;
  err = a0_stream_alloc(&lk, serial_size, &write_space);
  if (err) {
    a0_unlock_stream(&lk);
    return err;
  }
  a0_packet_serialize(pkt, &write_space);

  err = a0_stream_commit(&lk);
  if (err) {
    a0_unlock_stream(&lk);
    return err;
  }

  return a0_unlock_stream(&lk);
}
