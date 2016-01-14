#include "Video.h"

Video::Video(Memory *memory, CPU *cpu) {
    this->memory = memory;
    this->cpu = cpu;
    this->createSDLWindow();
    this->resetFrameBuffer();
    this->renderGame();
    this->scanlineCounter = MAX_VIDEO_CYCLES;
}

void Video::updateGraphics(short cycles) {

//    printVideoRegistersState();
    
    if (isLCDEnabled()) {
        scanlineCounter -= cycles;
        
        byte currentScanline = memory->readDirectly(LY_REGISTER);
        byte lcdStatus = memory->readDirectly(LCD_STATUS);
        mode = getLCDMode();

        if (currentScanline <= VERTICAL_BLANK_SCAN_LINE) {
            if (scanlineCounter >= (MAX_VIDEO_CYCLES - MIN_OAM_MODE_CYCLES)) {
                handleOAMMode();
            } else if (scanlineCounter >= (MAX_VIDEO_CYCLES - MIN_OAM_AND_VRAM_MODE_CYCLES - MIN_OAM_MODE_CYCLES)) {
                handleOAMAndVRamMode();
            } else {
                handleHBlankMode();
            }
        } else {
            handleVBlankMode();
        }
        
        byte lyRegister = memory->readDirectly(LY_REGISTER);
        byte lyCompare = memory->readDirectly(LY_COMPARE);
        if (lyCompare == lyRegister) {
            if (isBitSet(lcdStatus, 6)) {
                cpu->requestInterrupt(CPU::Interrupts::LCD);
            }
        }
        
    } else {
        scanlineCounter = MAX_VIDEO_CYCLES;
        byte lcdStatus = memory->readDirectly(LCD_STATUS);
        clearBit(&lcdStatus, 1);
        setBit(&lcdStatus, 0);
        memory->writeDirectly(LY_REGISTER, lcdStatus);
    }
}

void Video::handleHBlankMode() {
    byte lcdStatus = memory->readDirectly(LCD_STATUS);
    byte scanline = memory->readDirectly(LY_REGISTER);
    memory->writeDirectly(LY_REGISTER, ++scanline);
    
    if (scanline < VERTICAL_BLANK_SCAN_LINE) {
        drawScanline();
    } else {
        clearBit(&lcdStatus, 1);
        setBit(&lcdStatus, 0);
        memory->writeDirectly(LCD_STATUS, lcdStatus);
        
        if (isBitSet(lcdStatus, 4)) {
            cpu->requestInterrupt(CPU::Interrupts::VBLANK);
        }
    }
}

void Video::handleVBlankMode() {
    byte lcdStatus = memory->readDirectly(LCD_STATUS);
    byte scanline = memory->readDirectly(LY_REGISTER);
    memory->writeDirectly(LY_REGISTER, ++scanline);
    
    if (scanline == VERTICAL_BLANK_SCAN_LINE_MAX) {
        scanlineCounter = MAX_VIDEO_CYCLES;
        memory->writeDirectly(LY_REGISTER, 0x0);
        setBit(&lcdStatus, 1);
        clearBit(&lcdStatus, 0);
        memory->writeDirectly(LCD_STATUS, lcdStatus);
        
        
        if (isBitSet(lcdStatus, 5)) {
            cpu->requestInterrupt(CPU::Interrupts::LCD);
        }
    }
}

void Video::handleOAMMode() {
    byte lcdStatus = memory->readDirectly(LCD_STATUS);
    setBit(&lcdStatus, 1);
    clearBit(&lcdStatus, 0);
    memory->writeDirectly(LCD_STATUS, lcdStatus);
    
    
    if (isBitSet(lcdStatus, 3)) {
        cpu->requestInterrupt(CPU::Interrupts::LCD);
    }

}

void Video::handleOAMAndVRamMode() {
    byte lcdStatus = memory->readDirectly(LCD_STATUS);
    setBit(&lcdStatus,1);
    setBit(&lcdStatus,0);
    memory->writeDirectly(LCD_STATUS, lcdStatus);
}

bool Video::isLCDEnabled() const {
    return isBitSet(memory->read(LCDS_CONTROL), 7);
}

void Video::drawScanline() {
    byte lcdControl = memory->readDirectly(LCDS_CONTROL);
	if ( isBitSet(lcdControl, 0) ) {
		renderBackground(lcdControl);
    } else if ( isBitSet(lcdControl, 1) ) {
        renderSprites(lcdControl);
    }
}

void Video::renderBackground(byte lcdControl) {

    if ( isBitSet(lcdControl,0) ) {
		word tileData = 0 ;
		word backgroundMemory =0 ;
		bool unsig = true ;

		byte scrollY = memory->read(SCROLL_Y);
		byte scrollX = memory->read(SCROLL_X);
		byte windowY = memory->read(WINDOWS_Y);
		byte windowX = memory->read(WINDOWS_X) - 7;
        bool usingWindow = false;

		if ( isBitSet(lcdControl, 5) ) {
			if (windowY <= memory->read(LY_REGISTER) ) {
				usingWindow = true;
            }
		}

		if ( isBitSet(lcdControl,4) ) {
			tileData = 0x8000;
		} else {
			tileData = 0x8800;
			unsig= false;
		}

		if ( !usingWindow ) {
			if ( isBitSet(lcdControl, 3) ) {
				backgroundMemory = 0x9C00;
			} else {
				backgroundMemory = 0x9800;
            }
		} else {
			if ( isBitSet(lcdControl, 6) ) {
				backgroundMemory = 0x9C00;
            } else {
				backgroundMemory = 0x9800;
            }
		}


		byte yPos = 0;

		if ( !usingWindow ) {
			yPos = scrollY + memory->read(LY_REGISTER);
		} else {
			yPos = memory->read(LY_REGISTER) - windowY;
        }

        word tileRow = (((byte)(yPos/8))*32);

		for ( int pixel = 0 ; pixel < 160; pixel++ ) {
			byte xPos = pixel+scrollX;

			if ( usingWindow ) {
				if ( pixel >= windowX ) {
					xPos = pixel - windowX;
				}
			}

			word tileCol = (xPos/8);
			signed short tileNum;

			if( unsig ) {
				tileNum = (byte)memory->read(backgroundMemory+tileRow + tileCol);
			} else {
				tileNum = (signed short)memory->read(backgroundMemory+tileRow + tileCol);
            }

			word tileLocation = tileData;

			if ( unsig ) {
				tileLocation += (tileNum * 16);
			} else {
				tileLocation += ((tileNum+128) *16);
            }

			byte line = yPos % 8;
			line *= 2;
			byte data1 = memory->read(tileLocation + line);
			byte data2 = memory->read(tileLocation + line + 1);

			int colourBit = xPos % 8;
			colourBit -= 7;
			colourBit *= -1;

            int colourNum = getBitValue(data2, colourBit);
			colourNum <<= 1;
            colourNum |= getBitValue(data1,colourBit);

			Colour col = getColour(colourNum, 0xFF47);
			int red, green, blue = red = green = 0;

			switch( col ) {
                case white:	    red = 255;  green = 255;  blue = 255;  break;
                case lightGray: red = 0xCC; green = 0xCC; blue = 0xCC; break;
                case darkGray:	red = 0x77; green = 0x77; blue = 0x77; break;
                case black:     red = 0x0;  green = 0x0;  blue = 0x0;  break;
			}

			int scanline = memory->read(LY_REGISTER);

			frameBuffer[scanline][pixel][0] = red;
			frameBuffer[scanline][pixel][1] = green;
			frameBuffer[scanline][pixel][2] = blue;
		}
	}
}

Video::Colour Video::getColour(const byte colourNumber, const word address) const {
	Colour res = white;
	byte palette = memory->readDirectly(address);
	int hi = 0;
	int lo = 0;

	switch ( colourNumber ) {
        case 0: hi = 1; lo = 0; break;
        case 1: hi = 3; lo = 2; break;
        case 2: hi = 5; lo = 4; break;
        case 3: hi = 7; lo = 6; break;
	}

	int colour = 0;
	colour = getBitValue(palette, hi) << 1;
	colour |= getBitValue(palette, lo) ;

	switch ( colour ) {
        case 0: res = white;       break;
        case 1: res = lightGray;   break;
        case 2: res = darkGray;    break;
        case 3: res = black;       break;
	}

	return res;
}

void Video::renderSprites(byte lcdControl) {

	if ( isBitSet(lcdControl, 1) ) {
		bool use8x16 = false ;

        if ( isBitSet(lcdControl, 2) ) {
			use8x16 = true ;
        }

		for (int sprite = 0 ; sprite < 40; sprite++) {
 			byte index = sprite*4 ;
 			byte yPos = memory->read(0xFE00+index) - 16;
 			byte xPos = memory->read(0xFE00+index+1)-8;
 			byte tileLocation = memory->read(0xFE00+index+2) ;
 			byte attributes = memory->read(0xFE00+index+3) ;

            bool yFlip =  isBitSet(attributes, 6);
			bool xFlip = isBitSet(attributes, 5);
            bool hidden = isBitSet(attributes, 7);

			int scanline = memory->read(LY_REGISTER);

			int ysize = 8;

			if ( use8x16 ) {
				ysize = 16;
            }

 			if ( (scanline >= yPos) && (scanline < (yPos+ysize)) ) {
                int line = scanline - yPos;

 				if ( yFlip ) {
 					line -= ysize;
 					line *= -1;
 				}

 				line *= 2;
 				byte data1 = memory->read((0x8000 + (tileLocation * 16)) + line);
 				byte data2 = memory->read((0x8000 + (tileLocation * 16)) + line+1);

 				for ( int tilePixel = 7; tilePixel >= 0; tilePixel-- ) {
					int colourbit = tilePixel ;
 					if ( xFlip ) {
 						colourbit -= 7;
 						colourbit *= -1;
 					}

                    int colourNum = getBitValue(data2,colourbit) ;
                    colourNum <<= 1;
                    colourNum |= getBitValue(data1,colourbit) ;

                    Colour color;
                    if ( isBitSet(attributes, 4) ) {
                        color = getColour(colourNum, 0xFF49);
                    } else {
                        color = getColour(colourNum, 0xFF48);
                    }

 					int red, green, blue;
                    red = green = blue = 0;

                    switch ( color ) {
                        case white:	    red = 255;  green = 255;  blue = 255;  break;
                        case lightGray: red = 0xCC; green = 0xCC; blue = 0xCC; break;
                        case darkGray:	red = 0x77; green = 0x77; blue = 0x77; break;
                        case black: red = 0x0; green = 0x0; blue = 0x0; break;
                    }

 					int xPix = 0 - tilePixel;
 					xPix += 7;

					int pixel = xPos+xPix;

                    if ( hidden && frameBuffer[scanline][pixel][0] == white ) {
                        hidden = false;
                    }

                    if ( !hidden ){
                        frameBuffer[scanline][pixel][0] = red;
                        frameBuffer[scanline][pixel][1] = green;
                        frameBuffer[scanline][pixel][2] = blue;
                    }
 				}
 			}
		}
	}
}

byte Video::getLCDMode() const {
    byte lcdStatus = memory->readDirectly(LCD_STATUS);
	return lcdStatus & 0x3;
}


bool Video::createSDLWindow() {
    if( SDL_Init( SDL_INIT_EVERYTHING ) < 0 ) {
        return false;
    }
    
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32);
    
    window = SDL_CreateWindow("PatBoy",  0,
                              0,GAMEBOY_WIDTH, GAMEBOY_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(window);
    initializeOpenGL();
    return true ;
}

void Video::initializeOpenGL() {
    glViewport(0, 0, GAMEBOY_WIDTH, GAMEBOY_HEIGHT);
    glMatrixMode(GL_MODELVIEW);
    glOrtho(0, GAMEBOY_WIDTH, GAMEBOY_HEIGHT, 0, -1.0, 1.0);
    glClearColor(0, 0, 0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glShadeModel(GL_FLAT);
    
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
}

void Video::renderGame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glRasterPos2i(-1, 1);
    glPixelZoom(1, -1);
    glDrawPixels(GAMEBOY_WIDTH, GAMEBOY_HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, this->frameBuffer);
    SDL_GL_SwapWindow(window);
}

void Video::resetFrameBuffer() {
    for ( int x = 0 ; x < 144; x++ ) {
        for ( int y = 0; y < 160; y++ ) {
            frameBuffer[x][y][0] = 255;
            frameBuffer[x][y][1] = 255;
            frameBuffer[x][y][2] = 255;
        }
    }
}

void Video::printVideoRegistersState() const {
    printf("LCDS CONTROL: %02X\t", memory->readDirectly(LCDS_CONTROL));
    printf("LCD STATUS: %02X\t", memory->readDirectly(LCD_STATUS));
    printf("LY_REGISTER: %02X\t" , memory->readDirectly(LY_REGISTER));
    printf("LY_COMPARE: %02X\t", memory->readDirectly(LY_COMPARE));
    printf("SCROLL X: %02X\t", memory->readDirectly(SCROLL_X));
    printf("SCROLL Y: %02X\n\n", memory->readDirectly(SCROLL_Y));
}

