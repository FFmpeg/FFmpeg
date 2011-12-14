FATE_TESTS += fate-indeo2
fate-indeo2: CMD = framecrc -i $(SAMPLES)/rt21/VPAR0026.AVI

FATE_TESTS += fate-indeo3
fate-indeo3: CMD = framecrc -i $(SAMPLES)/iv32/cubes.mov

FATE_TESTS += fate-indeo5
fate-indeo5: CMD = framecrc -i $(SAMPLES)/iv50/Educ_Movie_DeadlyForce.avi -an
