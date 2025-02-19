#include "sbus.h"

Sbus::Sbus(AbstractSerial &serial) : serial_(serial)
{
}

Sbus::~Sbus()
{
}

SensorSbus *Sbus::sensorSbusP[32] = {NULL};

const uint8_t Sbus::slotId[32] = {0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
                                  0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
                                  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
                                  0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB};

volatile uint8_t Sbus::slotCont = 0;

uint8_t Sbus::telemetryPacket = 0;

uint32_t Sbus::ts2 = 0;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(ARDUINO_AVR_A_STAR_328PB) || defined(__AVR_ATmega2560__)

void Sbus::TIMER_COMP_handler()
{
    uint8_t ts = TCNT2;
#ifdef DEBUG_SBUS_MS
    static uint16_t msprev = 0;
    uint16_t ms = micros();
    if (slotCont == 0)
    {
        DEBUG_PRINT(" ");
        DEBUG_PRINT(micros() - ts2);
    }
    else
    {
        DEBUG_PRINT(" ");
        DEBUG_PRINT((uint16_t)(ms - msprev));
    }
    msprev = ms;
#endif
#ifdef DEBUG
    DEBUG_PRINT(" ");
    DEBUG_PRINT(telemetryPacket * 8 + slotCont);
#endif
    if (sensorSbusP[telemetryPacket * 8 + slotCont] != NULL)
        sendSlot(telemetryPacket * 8 + slotCont);
    if (slotCont < 7)
    {
        OCR2A = ts + (uint8_t)(SBUS_INTER_SLOT_DELAY * US_TO_COMP(256));
        slotCont++;
    }
    else
    {
        TIMSK2 &= ~_BV(OCIE2A); // DISABLE TIMER2 OCRA INTERRUPT
        slotCont = 0;
    }
}

#endif

#if defined(__AVR_ATmega32U4__)

void Sbus::TIMER_COMP_handler()
{
    uint16_t ts = TCNT3;
#ifdef DEBUG_SBUS_MS
    static uint16_t msprev = 0;
    uint16_t ms = micros();
    if (slotCont == 0)
    {
        DEBUG_PRINT(" ");
        DEBUG_PRINT(micros() - ts2);
    }
    else
    {
        DEBUG_PRINT(" ");
        DEBUG_PRINT((uint16_t)(ms - msprev));
    }
    msprev = ms;
#endif
#ifdef DEBUG
    DEBUG_PRINT(" ");
    DEBUG_PRINT(telemetryPacket * 8 + slotCont);
#endif

    if (sensorSbusP[telemetryPacket * 8 + slotCont] != NULL)
        sendSlot(telemetryPacket * 8 + slotCont);
    if (slotCont < 7)
    {
        OCR3B = ts + (uint16_t)(SBUS_INTER_SLOT_DELAY * US_TO_COMP(1));
        slotCont++;
    }
    else
    {
        TIMSK3 &= ~_BV(OCIE3B); // DISABLE TIMER2 OCRA INTERRUPT
        slotCont = 0;
    }
}

#endif

#if defined(__MKL26Z64__) || defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__)
void Sbus::FTM0_IRQ_handler()
{
    if (FTM0_C0SC & FTM_CSC_CHF) // CH0 INTERRUPT
    {
        uint16_t ts = FTM0_CNT;
#ifdef DEBUG_SBUS_MS
        static uint16_t msprev = 0;
        uint16_t ms = micros();
        if (slotCont == 0)
        {
            DEBUG_PRINT(" ");
            DEBUG_PRINT(micros() - ts2);
        }
        else
        {
            DEBUG_PRINT(" ");
            DEBUG_PRINT((uint16_t)(ms - msprev));
        }
        msprev = ms;
#endif
#ifdef DEBUG
        DEBUG_PRINT(" ");
        DEBUG_PRINT(telemetryPacket * 8 + slotCont);
#endif
        if (sensorSbusP[telemetryPacket * 8 + slotCont] != NULL)
            sendSlot(telemetryPacket * 8 + slotCont);
        if (slotCont < 7)
        {
            FTM0_C0V = (uint16_t)(ts + SBUS_INTER_SLOT_DELAY * US_TO_COMP(128));
            slotCont++;
        }
        else
        {
            FTM0_C0SC &= ~FTM_CSC_CHIE; // DISABLE CH0 INTERRUPT
            slotCont = 0;
        }
        FTM0_C0SC |= FTM_CSC_CHF; // CLEAR FLAG CH2
    }
}

#endif

void Sbus::sendSlot(uint8_t number)
{
    digitalWrite(LED_BUILTIN, HIGH);
    uint16_t val = sensorSbusP[number]->valueFormatted();
    uint8_t buffer[3];
    buffer[0] = slotId[number];
    buffer[1] = val;
    buffer[2] = val >> 8;
    SMARTPORT_FRSKY_SBUS_SERIAL.writeBytes(buffer, 3);
    digitalWrite(LED_BUILTIN, LOW);
#ifdef DEBUG
    DEBUG_PRINT(":");
    DEBUG_PRINT_HEX(slotId[number]);
    DEBUG_PRINT(":");
    DEBUG_PRINT_HEX((uint8_t)val);
    DEBUG_PRINT(":");
    DEBUG_PRINT_HEX((uint8_t)(val >> 8));
#endif
}

void Sbus::begin()
{
    deviceBufferP = new CircularBuffer<Device>;
    SMARTPORT_FRSKY_SBUS_SERIAL.begin(100000, SERIAL__8E2_RXINV_TXINV | SERIAL__HALF_DUP);
    //SMARTPORT_FRSKY_SBUS_SERIAL.begin(100000, SERIAL__8E2);
    SMARTPORT_FRSKY_SBUS_SERIAL.setTimeout(SBUS_SERIAL_TIMEOUT);
    pinMode(LED_BUILTIN, OUTPUT);
    setConfig();

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(ARDUINO_AVR_A_STAR_328PB) || defined(__AVR_ATmega2560__)
    // TIMER 2 - shared with softserial
    TIMER2_COMPA_handlerP = TIMER_COMP_handler;
    TCCR2B = _BV(CS22) | _BV(CS21); // SCALER 256
    TCCR2A = 0;
#endif

#if defined(__AVR_ATmega32U4__)
    // TIMER 3
    TIMER3_COMPB_handlerP = TIMER_COMP_handler;
    TCCR3B = _BV(CS30); // SCALER 1
    TCCR3A = 0;
#endif

#if defined(__MKL26Z64__) || defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__)
    FTM0_IRQ_handlerP = FTM0_IRQ_handler;
    FTM0_SC = 0;
    delayMicroseconds(1);
    FTM0_CNT = 0;
    SIM_SCGC6 |= SIM_SCGC6_FTM0; // ENABLE CLOCK
    FTM0_MOD = 0xFFFF;
    FTM0_SC = FTM_SC_PS(7);      // PRESCALER 128    FTM_SC_PS(5) - PRESCALER 32
    FTM0_SC |= FTM_SC_CLKS(1);   // ENABLE COUNTER
    FTM0_C0SC = 0;               // DISABLE CHANNEL
    delayMicroseconds(1);
    FTM0_C0SC = FTM_CSC_MSA; // SOFTWARE COMPARE
    NVIC_ENABLE_IRQ(IRQ_FTM0);
#endif
}

void Sbus::addSensor(uint8_t number, SensorSbus *newSensorSbusP)
{
    sensorSbusP[number] = newSensorSbusP;
}

void Sbus::sendPacket()
{
    slotCont = 0;
#ifdef SIM_RX
    uint16_t ts = 1500;
#else
    uint16_t ts = SMARTPORT_FRSKY_SBUS_SERIAL.timestamp();
#endif
#ifdef DEBUG_SBUS_MS
    DEBUG_PRINTLN();
    DEBUG_PRINT(ts);
    DEBUG_PRINT("+");
    ts2 = micros();
#endif

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(ARDUINO_AVR_A_STAR_328PB) || defined(__AVR_ATmega2560__)
    // configure timer
    OCR2A = TCNT2 + (SBUS_SLOT_0_DELAY - ts) * US_TO_COMP(256); // complete 2ms
    TIFR2 |= _BV(OCF2A);                                        // CLEAR TIMER2 OCRA CAPTURE FLAG
    TIMSK2 |= _BV(OCIE2A);                                      // ENABLE TIMER2 OCRA INTERRUPT
#endif

#if defined(__AVR_ATmega32U4__)
    // configure timer
    OCR3A = TCNT3 + (SBUS_SLOT_0_DELAY - ts) * US_TO_COMP(1); // complete 2ms
    TIFR3 |= _BV(OCF3A);                                      // CLEAR TIMER2 OCRA CAPTURE FLAG
    TIMSK3 |= _BV(OCIE3A);                                    // ENABLE TIMER2 OCRA INTERRUPT
#endif

#if defined(__MKL26Z64__) || defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__)
    FTM0_C0V = FTM0_CNT + (SBUS_SLOT_0_DELAY - ts) * US_TO_COMP(128);
    FTM0_C0SC |= FTM_CSC_CHF;  // CLEAR FLAG
    FTM0_C0SC |= FTM_CSC_CHIE; // ENABLE INTERRUPT
#endif
}

void Sbus::update()
{
    uint8_t status = SBUS_WAIT;
#if defined(SIM_RX)
    static uint16_t ts = 0;
    if ((uint16_t)(millis() - ts) > 500)
    {
        status = SBUS_SEND;
        telemetryPacket++;
        if (telemetryPacket == 4)
            telemetryPacket = 0;
        ts = millis();
#if defined(DEBUG) //|| defined(DEBUG_SBUS_MS)
        DEBUG_PRINTLN();
        DEBUG_PRINT(telemetryPacket);
        DEBUG_PRINT(": ");
#endif
    }
#else
    static bool mute = true;
    uint8_t lenght = SMARTPORT_FRSKY_SBUS_SERIAL.availableTimeout();
    uint8_t buff[lenght];
    SMARTPORT_FRSKY_SBUS_SERIAL.readBytes(buff, lenght);
#ifdef DEBUG_PACKET
    if (lenght)
    {
        DEBUG_PRINT("\n<");
        for (uint8_t i = 0; i < lenght; i++)
        {
            DEBUG_PRINT_HEX(buff[i]);
            DEBUG_PRINT(" ");
        }
    }
#endif
    if (lenght == SBUS_PACKET_LENGHT)
    {
        //uint8_t buff[SBUS_PACKET_LENGHT];
        //SMARTPORT_FRSKY_SBUS_SERIAL.readBytes(buff, SBUS_PACKET_LENGHT);
        if (buff[0] == 0x0F)
        {
#if defined(DEBUG) //|| defined(DEBUG_SBUS_MS)
            DEBUG_PRINTLN();
            DEBUG_PRINT_HEX(buff[24]);
            DEBUG_PRINT(": ");
#endif
            // SBUS
            if (buff[24] == 0)
            {
                if (!mute)
                {
                    status = SBUS_SEND;
                    telemetryPacket++;
                    if (telemetryPacket == 4)
                        telemetryPacket = 0;
                }
                mute = !mute;
            }
            // SBUS2
            else if (buff[24] == 0x04 || buff[24] == 0x14 || buff[24] == 0x24 || buff[24] == 0x34)
            {
                telemetryPacket = buff[24] >> 4;
                status = SBUS_SEND;
            }
        }
    }
#endif
    if (status == SBUS_SEND)
    {
        sendPacket();
    }

    // update sensor
    if (deviceBufferP->current())
    {
        deviceBufferP->current()->update();
        deviceBufferP->next();
    }
}

void Sbus::setConfig()
{
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_PWM)
    {
        SensorSbus *sensorSbusP;
        EscPWM *esc;
        esc = new EscPWM(ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM));
        esc->begin();
        deviceBufferP->add(esc);
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_HW_V3)
    {
        SensorSbus *sensorSbusP;
        EscHW3 *esc;
        esc = new EscHW3(ESC_SERIAL, ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM));
        esc->begin();
        deviceBufferP->add(esc);
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_HW_V4)
    {
        SensorSbus *sensorSbusP;
        EscHW4 *esc;
        esc = new EscHW4(ESC_SERIAL, ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM), ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP), 0);
        esc->begin();
        deviceBufferP->add(esc);
        PwmOut pwmOut;
        pwmOut.setRpmP(esc->rpmP());
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->currentP());
        addSensor(SBUS_SLOT_POWER_CURR1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->voltageP());
        addSensor(SBUS_SLOT_POWER_VOLT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, esc->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempFetP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempBecP());
        addSensor(SBUS_SLOT_TEMP2, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->cellVoltageP());
        //addSensor(12, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_CASTLE)
    {
        SensorSbus *sensorSbusP;
        EscCastle *esc;
        esc = new EscCastle(ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM), ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        esc->begin();
        deviceBufferP->add(esc);
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->currentP());
        addSensor(SBUS_SLOT_POWER_CURR1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->voltageP());
        addSensor(SBUS_SLOT_POWER_VOLT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, esc->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_VOLT_V1, esc->rippleVoltageP());
        //addSensor(SBUS_SLOT_VOLT_V1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->becCurrentP());
        //addSensor(SBUS_SLOT_POWER_CURR2, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->becVoltageP());
        //addSensor(SBUS_SLOT_POWER_VOLT2, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->temperatureP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->cellVoltageP());
        //addSensor(12, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_KONTRONIK)
    {
        SensorSbus *sensorSbusP;
        EscKontronik *esc;
        esc = new EscKontronik(ESC_SERIAL, ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM), ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        esc->begin();
        deviceBufferP->add(esc);
        //PwmOut pwmOut;
        //pwmOut.setRpmP(esc->rpmP());
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->currentP());
        addSensor(SBUS_SLOT_POWER_CURR1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->voltageP());
        addSensor(SBUS_SLOT_POWER_VOLT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, esc->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->becCurrentP());
        //addSensor(SBUS_SLOT_POWER_CURR2, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->becVoltageP());
        //addSensor(SBUS_SLOT_POWER_VOLT2, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempFetP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempBecP());
        addSensor(SBUS_SLOT_TEMP2, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->cellVoltageP());
        //addSensor(12, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_APD_F)
    {
        SensorSbus *sensorSbusP;
        EscApdF *esc;
        esc = new EscApdF(ESC_SERIAL, ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM), ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        esc->begin();
        deviceBufferP->add(esc);
        //PwmOut pwmOut;
        //pwmOut.setRpmP(esc->rpmP());
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->currentP());
        addSensor(SBUS_SLOT_POWER_CURR1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->voltageP());
        addSensor(SBUS_SLOT_POWER_VOLT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, esc->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->cellVoltageP());
        //addSensor(12, sensorSbusP);
    }
    if (CONFIG_ESC_PROTOCOL == PROTOCOL_APD_HV)
    {
        SensorSbus *sensorSbusP;
        EscApdHV *esc;
        esc = new EscApdHV(ESC_SERIAL, ALPHA(CONFIG_AVERAGING_ELEMENTS_RPM), ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        esc->begin();
        deviceBufferP->add(esc);
        //PwmOut pwmOut;
        //pwmOut.setRpmP(esc->rpmP());
        sensorSbusP = new SensorSbus(FASST_RPM, esc->rpmP());
        addSensor(SBUS_SLOT_RPM, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, esc->currentP());
        addSensor(SBUS_SLOT_POWER_CURR1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->voltageP());
        addSensor(SBUS_SLOT_POWER_VOLT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, esc->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_TEMP, esc->tempP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
        //sensorSbusP = new SensorSbus(FASST_POWER_VOLT, esc->cellVoltageP());
        //addSensor(12, sensorSbusP);
    }
    if (CONFIG_GPS)
    {
        SensorSbus *sensorSbusP;
        Bn220 *gps;
        gps = new Bn220(GPS_SERIAL, GPS_BAUD_RATE);
        gps->begin();
        deviceBufferP->add(gps);
        sensorSbusP = new SensorSbus(FASST_GPS_SPEED, gps->spdP());
        addSensor(SBUS_SLOT_GPS_SPD, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_ALTITUDE, gps->altP());
        addSensor(SBUS_SLOT_GPS_ALT, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_TIME, gps->timeP());
        addSensor(SBUS_SLOT_GPS_TIME, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_VARIO_SPEED, gps->varioP());
        addSensor(SBUS_SLOT_GPS_VARIO, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_LATITUDE1, gps->latP());
        addSensor(SBUS_SLOT_GPS_LAT1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_LATITUDE2, gps->latP());
        addSensor(SBUS_SLOT_GPS_LAT2, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_LONGITUDE1, gps->lonP());
        addSensor(SBUS_SLOT_GPS_LON1, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_GPS_LONGITUDE2, gps->lonP());
        addSensor(SBUS_SLOT_GPS_LON2, sensorSbusP);
    }
    if (CONFIG_AIRSPEED)
    {
        SensorSbus *sensorSbusP;
        Pressure *pressure;
        pressure = new Pressure(PIN_PRESSURE, ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT));
        deviceBufferP->add(pressure);
        sensorSbusP = new SensorSbus(FASST_AIR_SPEED, pressure->valueP());
        addSensor(SBUS_SLOT_AIR_SPEED, sensorSbusP);
    }
    if (CONFIG_VOLTAGE1)
    {
        SensorSbus *sensorSbusP;
        Voltage *voltage;
        voltage = new Voltage(PIN_VOLTAGE1, ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), VOLTAGE1_MULTIPLIER);
        deviceBufferP->add(voltage);
        sensorSbusP = new SensorSbus(FASST_VOLT_V1, voltage->valueP());
        addSensor(SBUS_SLOT_VOLT_V1, sensorSbusP);
        if (CONFIG_VOLTAGE2 == false)
        {
            sensorSbusP = new SensorSbus(FASST_VOLT_V2, NULL);
            addSensor(SBUS_SLOT_VOLT_V2, sensorSbusP);
        }
    }
    if (CONFIG_VOLTAGE2)
    {
        SensorSbus *sensorSbusP;
        Voltage *voltage;
        voltage = new Voltage(PIN_VOLTAGE2, ALPHA(CONFIG_AVERAGING_ELEMENTS_VOLT), VOLTAGE2_MULTIPLIER);
        deviceBufferP->add(voltage);
        sensorSbusP = new SensorSbus(FASST_VOLT_V2, voltage->valueP());
        addSensor(SBUS_SLOT_VOLT_V2, sensorSbusP);
        if (CONFIG_VOLTAGE1 == false)
        {
            sensorSbusP = new SensorSbus(FASST_VOLT_V1, NULL);
            addSensor(SBUS_SLOT_VOLT_V1, sensorSbusP);
        }
    }
    if (CONFIG_CURRENT)
    {
        SensorSbus *sensorSbusP;
        Current *current;
        current = new Current(PIN_CURRENT, ALPHA(CONFIG_AVERAGING_ELEMENTS_CURR), CURRENT_MULTIPLIER, CURRENT_OFFSET, CURRENT_AUTO_OFFSET);
        deviceBufferP->add(current);
        sensorSbusP = new SensorSbus(FASST_POWER_CURR, current->valueP());
        addSensor(SBUS_SLOT_POWER_CURR2, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_CONS, current->consumptionP());
        addSensor(SBUS_SLOT_POWER_CONS2, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_POWER_VOLT, NULL);
        addSensor(SBUS_SLOT_POWER_VOLT2, sensorSbusP);
    }
    if (CONFIG_NTC1)
    {
        SensorSbus *sensorSbusP;
        Ntc *ntc;
        ntc = new Ntc(PIN_NTC1, ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        deviceBufferP->add(ntc);
        sensorSbusP = new SensorSbus(FASST_TEMP, ntc->valueP());
        addSensor(SBUS_SLOT_TEMP1, sensorSbusP);
    }
    if (CONFIG_NTC2)
    {
        SensorSbus *sensorSbusP;
        Ntc *ntc;
        ntc = new Ntc(PIN_NTC2, ALPHA(CONFIG_AVERAGING_ELEMENTS_TEMP));
        deviceBufferP->add(ntc);
        sensorSbusP = new SensorSbus(FASST_TEMP, ntc->valueP());
        addSensor(SBUS_SLOT_TEMP2, sensorSbusP);
    }
    if (CONFIG_I2C1_TYPE == I2C_BMP280)
    {
        SensorSbus *sensorSbusP;
        Bmp280 *bmp;
        bmp = new Bmp280(CONFIG_I2C1_ADDRESS, ALPHA(CONFIG_AVERAGING_ELEMENTS_VARIO));
        bmp->begin();
        deviceBufferP->add(bmp);
        sensorSbusP = new SensorSbus(FASST_VARIO_SPEED, bmp->varioP());
        addSensor(SBUS_SLOT_VARIO_SPEED, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_VARIO_ALT, bmp->altitudeP());
        addSensor(SBUS_SLOT_VARIO_ALT, sensorSbusP);
    }
    if (CONFIG_I2C1_TYPE == I2C_MS5611)
    {
        SensorSbus *sensorSbusP;
        MS5611 *bmp;
        bmp = new MS5611(CONFIG_I2C1_ADDRESS, ALPHA(CONFIG_AVERAGING_ELEMENTS_VARIO));
        bmp->begin();
        deviceBufferP->add(bmp);
        sensorSbusP = new SensorSbus(FASST_VARIO_SPEED, bmp->varioP());
        addSensor(SBUS_SLOT_VARIO_SPEED, sensorSbusP);
        sensorSbusP = new SensorSbus(FASST_VARIO_ALT, bmp->altitudeP());
        addSensor(SBUS_SLOT_VARIO_ALT, sensorSbusP);
    }
}
