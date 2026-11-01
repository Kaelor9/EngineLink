// NMEA 2000 (listen only): GPS speed and time over the isolated CAN interface.

uint32_t nmeaPGN(uint32_t canid){
  uint32_t pgn=(canid>>8)&0x3FFFF;
  if(((canid>>16)&0xFF)<240)pgn&=0x3FF00;
  return pgn;
}
bool startCAN(){
  if(canReady)return true;
  twai_general_config_t gen=TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX,(gpio_num_t)CAN_RX,TWAI_MODE_LISTEN_ONLY);
  gen.rx_queue_len=32;
  twai_timing_config_t timing=TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t filter=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if(twai_driver_install(&gen,&timing,&filter)!=ESP_OK)return false;
  if(twai_start()!=ESP_OK){twai_driver_uninstall();return false;}
  canReady=true;return true;
}
void stopCAN(){if(canReady){twai_stop();twai_driver_uninstall();canReady=false;}}
void serviceCAN(){
  if(!canReady)return;
  twai_message_t m;
  for(int k=0;k<12;k++){
    if(twai_receive(&m,0)!=ESP_OK)break;
    ++ctr.nmeaFrames;
    if(!m.extd||m.data_length_code!=8)continue;
    uint32_t pgn=nmeaPGN(m.identifier);
    if(pgn==129026){ // COG & SOG rapid update. SOG in 0.01 m/s units.
      uint16_t raw=(uint16_t)m.data[4]|((uint16_t)m.data[5]<<8);
      if(raw!=0xFFFF){sogKnots=((float)raw*0.01f)*1.94384449f;sogAt=elapsedMs();}
    }else if(pgn==126992){ // NMEA2000 System Time: days since 1970, 0.0001 sec/day
      uint16_t days=(uint16_t)m.data[2]|((uint16_t)m.data[3]<<8);
      uint32_t sec10000=(uint32_t)m.data[4]|((uint32_t)m.data[5]<<8)|((uint32_t)m.data[6]<<16)|((uint32_t)m.data[7]<<24);
      if(days!=0xFFFF&&sec10000!=0xFFFFFFFFUL&&sec10000<864000000UL&&allowTime("gps")){
        time_t epoch=(time_t)((uint32_t)days*86400UL+(sec10000/10000UL));
        if(epoch>1700000000) setClockFromUtc(epoch,"gps");
      }
    }
  }
}
