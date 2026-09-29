/* Requirements: ORT-INT-001, ORT-INT-005, ORT-INT-006, ORT-INT-007,
 * Requirements: ORT-INT-008, ORT-INT-009 */
#include <dds/dds.h>
#include <stdio.h>
#include <string.h>
#include "VendorProbe.h"

int main(int argc, char **argv) {
  const dds_entity_t participant = dds_create_participant(43, NULL, NULL);
  if (participant < 0) {
    fprintf(stderr, "Cyclone participant: %s\n", dds_strretcode(-participant));
    return 1;
  }
  const char *mode = argc > 1 ? argv[1] : "participant";
  const int reliable = strstr(mode, "reliable") != NULL;
  if (mode[0] == 'p' || mode[0] == 's' || mode[0] == 'r') {
    const dds_entity_t topic = dds_create_topic(
        participant, &VendorProbe_desc, "OpenRTDDSProbe", NULL, NULL);
    dds_qos_t *qos = dds_create_qos();
    dds_qset_reliability(qos,
        reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
        reliable ? DDS_SECS(1) : DDS_MSECS(0));
    const dds_entity_t endpoint = topic >= 0
        ? (mode[0] == 'p'
            ? dds_create_writer(participant, topic, qos, NULL)
            : dds_create_reader(participant, topic, qos, NULL)) : -1;
    dds_delete_qos(qos);
    if (endpoint < 0) {
      fprintf(stderr, "Cyclone endpoint creation failed: %d\n", endpoint);
      return 1;
    }
    if (strcmp(mode, "publish-data") == 0 ||
        strcmp(mode, "publish-reliable") == 0 ||
        strcmp(mode, "publish-openrtdds") == 0) {
      dds_sleepfor(DDS_SECS(3));
      const VendorProbe sample = {.value = 0x4F525444U};
      const unsigned attempts = strcmp(mode, "publish-openrtdds") == 0
          ? 4U : 1U;
      for (unsigned attempt = 0U; attempt < attempts; ++attempt) {
        const dds_return_t result = dds_write(endpoint, &sample);
        if (result != DDS_RETCODE_OK) {
          fprintf(stderr, "Cyclone sample write failed: %s\n",
                  dds_strretcode(-result));
          return 1;
        }
        if (attempt + 1U < attempts) {
          dds_sleepfor(DDS_MSECS(100));
        }
      }
      dds_sleepfor(reliable ? DDS_SECS(4) : DDS_SECS(2));
    } else if (strcmp(mode, "receive-openrtdds") == 0) {
      VendorProbe sample = {0};
      void *samples[1] = {&sample};
      dds_sample_info_t information[1] = {{0}};
      int accepted = 0;
      for (unsigned attempt = 0; attempt < 120U; ++attempt) {
        const dds_return_t received = dds_take(
            endpoint, samples, information, 1U, 1U);
        if (received < 0) {
          fprintf(stderr, "Cyclone take failed: %s\n",
                  dds_strretcode(-received));
          return 1;
        }
        if (received == 1 && information[0].valid_data &&
            sample.value == 0x4F525444U) {
          printf("Cyclone DDS received OpenRTDDS value=%u\n", sample.value);
          accepted = 1;
          break;
        }
        dds_sleepfor(DDS_MSECS(100));
      }
      if (!accepted) {
        fprintf(stderr, "Cyclone DDS did not receive OpenRTDDS sample\n");
        return 1;
      }
    } else {
      dds_sleepfor(reliable ? DDS_SECS(7) : DDS_SECS(5));
    }
  } else {
    dds_sleepfor(DDS_SECS(5));
  }
  return dds_delete(participant) == DDS_RETCODE_OK ? 0 : 1;
}
