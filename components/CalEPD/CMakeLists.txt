# Add only the display srcs you are going to use if you want to keep compiled srcs to a minimum:
set(srcs 
    "models/custom/custom042.cpp"
    "models/gdeh0213b73.cpp"
    # New GOODISPLAY models will be in goodisplay directory
    "models/goodisplay/gdey0213b74.cpp"
    "models/goodisplay/gdey0154d67.cpp"
    "models/goodisplay/gdey029T94.cpp"
    "models/goodisplay/gdey027T91.cpp"
    "models/goodisplay/gdey042T81.cpp"
    "models/goodisplay/gdeq037T31.cpp"
    "models/goodisplay/gdey075T7.cpp"
    # With touch integrated (please also enable FT6X36-IDF in the REQUIRE section)
    #"models/goodisplay/touch/gdey027T91T.cpp"
    #"models/gdew027w3T.cpp"
    
    # DKE epapers (New)
    "models/dke/depg1020bn.cpp"
	"models/dke/depg750bn.cpp"
    # GoodDisplay
    "models/wave12i48.cpp"
    "models/gdew075HD.cpp"
    "models/gdew075T7.cpp"
    "models/gdew075T7Grays.cpp"
    "models/gdew075T8.cpp"
    "models/gdew0583t7.cpp"
    "models/gdew042t2.cpp"
    "models/gdew042t2Grays.cpp"
    "models/gdem029E97.cpp"
    "models/gdew027w3.cpp"
    "models/wave12i48.cpp"
    "models/gdew0213i5f.cpp"
    "models/gdep015OC1.cpp"
    "models/gdeh0154d67.cpp"
    "models/heltec0151.cpp"
    
    # SMALL epapers
    "models/small/gdew0102I3F.cpp"
    
    # 3 colors Goodisplay
    "models/color/gdeh0154z90.cpp"
    "models/color/gdew0583z21.cpp"
    "models/color/gdew0583z83.cpp"
    "models/color/gdew075z09.cpp"
    "models/color/gdew075c64.cpp"
    "models/color/gdeh042Z96.cpp"
    "models/color/gdeh042Z21.cpp"
    "models/color/gdeq042Z21.cpp"
    "models/color/wave12i48BR.cpp"

    "models/color/dke/dke075z83.cpp"
    # 7 colors ACEP
    "models/color/gdey073d46.cpp"
    "models/color/wave4i7Color.cpp"
    "models/color/wave5i7Color.cpp"

    # Plasticlogic.com Flex epaper models:
    "models/plasticlogic/epdspi2cs.cpp"
    "models/plasticlogic/plasticlogic.cpp"
    "models/plasticlogic/plasticlogic011.cpp"
    "models/plasticlogic/plasticlogic014.cpp"
    "models/plasticlogic/plasticlogic021.cpp"
    # Not-tested (never had one)
    #"models/plasticlogic/plasticlogic031.cpp"

    # Parallel epapers supported by Epdiy driver (Uncomment epdParallel & REQUIRES "epdiy")
    # Uncomment for parallel epapers:
    #"epdParallel.cpp"
    #"models/parallel/ED047TC1.cpp"
    #"models/parallel/ED047TC1touch.cpp"
    #"models/parallel/ED060SC4.cpp"
    #"models/parallel/ED060XCN.cpp" # pending to be added

    # Common base classes
    "epd.cpp"
    "epd7color.cpp"
    "epdspi.cpp"
    "epd4spi.cpp"
    )

idf_build_get_property(target IDF_TARGET)
message(STATUS "→ Building example for ${target}")

if (${target} EQUAL "esp32")
    list(APPENDS srcs "epd4spi.cpp")
endif()

# If the project does not use a touch display component FT6X36-IDF can be removed or #commented
idf_component_register(SRCS ${srcs}      
                    REQUIRES "Adafruit-GFX"
                    #REQUIRES "FT6X36-IDF"
                    REQUIRES esp_timer 
                             driver
                    # Uncomment for parallel epapers:
                    # REQUIRES "epdiy"

                    INCLUDE_DIRS "include"
)
# Note about epdiy component: Even if it's not REQUIRED sometimes CMake includes it
#      giving errors. If that happens, just open the component CMakeLists and add:
#      return()
#      As the first line. This will tell CMake to not compile it.