# Self Test Script

import csi
import omv
import time

time.sleep(1)

# Camera sensor Self Test
print("Testing Camera...")
csi0 = csi.CSI()
csi0.reset()
csi0.pixformat(csi.RGB565)
csi0.framesize(csi.VGA)
print("Camera self-test passed.")

# IMU Self Test
if omv.arch() in ("OpenMV N6", "OPENMV AE3"):
    print("Testing IMU...")
    import imu
    imu.acceleration_mg()  # Errors out if IMU not found...
    print("IMU self-test passed.")

# ToF Self Test
if omv.arch() in ("OPENMV AE3"):
    print("Testing ToF...")
    import tof
    tof.init()  # Errors out if ToF not found...
    tof.deinit()
    print("ToF self-test passed.")

# WiFi Self Test
if omv.arch() in ("OMVRT1060 32768 SDRAM", "OpenMV N6", "OPENMV AE3"):
    print("Testing WiFi chip...")
    import network
    sta_if = network.WLAN(network.STA_IF)
    sta_if.active(True)
    if not len(sta_if.scan()):  # At least one network should be found.
        raise Exception("WiFi self-test failed.")
    sta_if.active(False)
    print("WiFi network self-test passed.")

# LAN Self Test
if omv.arch() in ():  # "OMVRT1060 32768 SDRAM"
    print("Testing LAN chip...")
    import network
    try:
        sta_if = network.LAN()
    except OSError as exception:
        if exception.args[0] != "PHY Auto-negotiation failed.":
            raise Exception("LAN self-test failed.")
    print("LAN network self-test passed.")

clock = time.clock()

while True:
    clock.tick()
    img = csi0.snapshot()
    print(clock.fps())
