#include "../cobs.h"
#include "byte_vec.h"
#include "doctest.h"


// http://www.stuartcheshire.org/papers/COBSforToN.pdf
TEST_CASE("COBS paper examples") {
  SUBCASE("Figure 2") {
    byte_vec_t input(680, 0x01);
    input[input.size() - 1] = 0x00;

    byte_vec_t output(684);
    unsigned output_len;

    REQUIRE(cobs_encode(input.data(),
                         static_cast< unsigned >(input.size()),
                         output.data(),
                         static_cast< unsigned >(output.size()),
                         &output_len) == COBS_RET_SUCCESS);


    byte_vec_t expected{0xFF};
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xAC);
    expected.insert(std::end(expected), 172, 0x01);
    expected.push_back(0x00);
    REQUIRE(output == expected);
  }

  SUBCASE("Figure 3") {
    byte_vec_t input {0x45,0x00,0x00,0x2C,0x4C,0x79,0x00,0x00,0x40,0x06,0x4F,0x37};
    byte_vec_t output
        {0x02,0x45,0x01,0x04,0x2C,0x4C,0x79,0x01,0x05,0x40,0x06,0x4F,0x37,0x00};

    char encoded[256];
    unsigned encoded_len;

    REQUIRE(cobs_encode(input.data(),
                        static_cast< unsigned >(input.size()),
                        encoded,
                        sizeof(encoded),
                        &encoded_len) == COBS_RET_SUCCESS);
    REQUIRE(output == byte_vec_t(encoded, encoded + encoded_len));
  }
}
