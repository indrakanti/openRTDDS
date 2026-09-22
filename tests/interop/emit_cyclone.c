/* Requirements: ORT-INT-001, ORT-INT-005 */
#include <dds/dds.h>
#include <stdio.h>
#include "VendorProbe.h"

int main(int argc, char **argv) {
  const dds_entity_t participant = dds_create_participant(43, NULL, NULL);
  if (participant < 0) {
    fprintf(stderr, "Cyclone participant: %s\n", dds_strretcode(-participant));
    return 1;
  }
  if (argc > 1 && argv[1][0] == 'p') {
    const dds_entity_t topic = dds_create_topic(
        participant, &VendorProbe_desc, "OpenRTDDSProbe", NULL, NULL);
    const dds_entity_t writer = topic >= 0
        ? dds_create_writer(participant, topic, NULL, NULL) : -1;
    if (writer < 0) {
      fprintf(stderr, "Cyclone writer creation failed: %d\n", writer);
      return 1;
    }
  }
  dds_sleepfor(DDS_SECS(5));
  return dds_delete(participant) == DDS_RETCODE_OK ? 0 : 1;
}
