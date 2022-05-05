void acWriteReg(uint8_t reg, uint16_t val) {
  esp_err_t ret = ESP_OK;
  uint8_t buf[2];
  buf[0] = uint8_t((val >> 8) & 0xff);
  buf[1] = uint8_t(val & 0xff);

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  ret |= i2c_master_start(cmd);
  ret |= i2c_master_write_byte(cmd, (AC101_ADDR << 1) | WRITE_BIT, ACK_CHECK_EN);
  ret |= i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
  ret |= i2c_master_write(cmd, buf, 2, ACK_CHECK_EN);
  ret |= i2c_master_stop(cmd);
  ret |= i2c_master_cmd_begin((i2c_port_t) I2C_MASTER_NUM, cmd, 1000 / portTICK_RATE_MS);
  i2c_cmd_link_delete(cmd);
}
