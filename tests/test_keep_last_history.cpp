#include <array>
#include <cstdint>

#include "openrtdds/core/keep_last_history.hpp"
#include "test_support.hpp"

void test_keep_last_history() {
  using History = openrtdds::core::KeepLastHistory<2U, 4U>;
  using openrtdds::core::HistoryError;
  using openrtdds::core::SampleMetadata;

  History history;
  const std::array<std::uint8_t, 2U> first{{1U, 2U}};
  const std::array<std::uint8_t, 1U> second{{3U}};
  const std::array<std::uint8_t, 3U> third{{4U, 5U, 6U}};

  CHECK(history.push(first.data(), first.size(), SampleMetadata{10U, 100U}) ==
        HistoryError::none);
  CHECK(history.push(second.data(), second.size(),
                     SampleMetadata{11U, 110U}) == HistoryError::none);
  CHECK(history.full());
  CHECK(history.oldest()->metadata().sequence_number == 10U);
  CHECK(history.newest()->metadata().sequence_number == 11U);

  CHECK(history.push(third.data(), third.size(), SampleMetadata{12U, 120U}) ==
        HistoryError::none);
  CHECK(history.size() == 2U);
  CHECK(history.oldest()->metadata().sequence_number == 11U);
  CHECK(history.newest()->metadata().sequence_number == 12U);
  CHECK(history.newest()->size() == third.size());
  CHECK(history.newest()->data()[2] == 6U);

  const std::array<std::uint8_t, 5U> oversized{{0U, 1U, 2U, 3U, 4U}};
  CHECK(history.push(oversized.data(), oversized.size(),
                     SampleMetadata{13U, 130U}) ==
        HistoryError::payload_too_large);
  CHECK(history.size() == 2U);
  CHECK(history.newest()->metadata().sequence_number == 12U);

  CHECK(history.pop_oldest());
  CHECK(history.oldest()->metadata().sequence_number == 12U);
  CHECK(history.pop_oldest());
  CHECK(history.empty());
  CHECK(!history.pop_oldest());
  CHECK(history.oldest() == nullptr);
  CHECK(history.newest() == nullptr);
}
