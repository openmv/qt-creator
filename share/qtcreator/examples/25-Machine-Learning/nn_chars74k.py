# Chars74K CNN Example
#
# This example shows off how to use the OpenMV Cam to detect
# characters 0-9 A-Z a-z. This example only works on the OpenMV Cam H7.

import sensor, image, time, os, nn

sensor.reset()                         # Reset and initialize the sensor.
sensor.set_pixformat(sensor.GRAYSCALE) # Set pixel format to GRAYSCALE
sensor.set_framesize(sensor.QVGA)      # Set frame size to QVGA (320x240)
sensor.set_windowing((128, 128))       # Set 128x128 window.
sensor.skip_frames(time=500)
sensor.set_auto_gain(False)
sensor.set_auto_exposure(False)

# Load chars74 network
net = nn.load('/fnt-chars74k.network') # works on printed font
# net = nn.load('/fnt-chars74k.network') # works on handwritten chars
# net = nn.load('/img-chars74k.network') # works on images of chars
labels = ['n/a', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9']
for i in range(ord('A'), ord('Z') + 1): labels.append(chr(i))
for i in range(ord('a'), ord('z') + 1): labels.append(chr(i))

clock = time.clock()                # Create a clock object to track the FPS.
while(True):
    clock.tick()                    # Update the FPS clock.
    img = sensor.snapshot()         # Take a picture and return the image.
	# Adjust the binary thresholds below if things aren't working - make sure characters are good.
    out = net.forward(img.binary([(200, 255)]), softmax=True)
    max_idx = out.index(max(out))
    score = int(out[max_idx]*100)
    if (score < 50):
        score_str = "??:??%"
    else:
        score_str = "%s:%d%% "%(labels[max_idx], score)
    img.draw_string(0, 0, score_str, color=(255, 0, 0))

    print(clock.fps())             # Note: OpenMV Cam runs about half as fast when connected
                                   # to the IDE. The FPS should increase once disconnected.
