#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

// Dimensiones de la pantalla del ZX Spectrum
#define WIDTH 256
#define HEIGHT 192

float _SCALE = 1;
int _switch_BW = 0;
int running = 1;

//The surface contained by the window
SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
//SDL_Surface* screenSurface = NULL;

// Paleta de colores del ZX Spectrum
Uint32 palette[16] = {
    0x000000, /* negro */
	0x0000bf, /* azul */
	0xbf0000, /* rojo */
	0xbf00bf, /* magenta */
	0x00bf00, /* verde */
	0x00bfbf, /* ciano */
	0xbfbf00, /* amarillo */
	0xbfbfbf, /* blanco */
	0x000000, /* negro brillante */
	0x0000ff, /* azul brillante */
	0xff0000, /* rojo brillante	*/
	0xff00ff, /* magenta brillante */
	0x00ff00, /* verde brillante */
	0x00ffff, /* ciano brillante */
	0xffff00, /* amarillo brillante */
	0xffffff  /* blanco brillante */
};

// Funci�n para cargar el archivo SCR
unsigned char* cargar_scr(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("No se pudo abrir el archivo");
        return NULL;
    }
    unsigned char* buffer = (unsigned char*)malloc(6912); // 256*192/8 = 6912 bytes
    fread(buffer, 1, 6912, file);
    fclose(file);
    return buffer;
}

int get_pixel_address(int x, int y) {
	int y76 = y & 0b11000000; //# third of screen
	int y53 = y & 0b00111000;
	int y20 = y & 0b00000111;
	int address = (y76 << 5) + (y20 << 8) + (y53 << 2) + (x >> 3);
	return address;
}

int get_attribute_address(int x, int y) {
	int y73 = y & 0b11111000;
	int address = (y73 << 2) + (x >> 3);
	return address;
}

unsigned char  get_byte(unsigned char* scr, int x, int y) {
	return scr[ get_pixel_address(x,y) ];
}

unsigned char  get_attribute(unsigned char* scr, int x, int y) {
	return scr[ get_attribute_address(x,y) + 6144 ];
}


// Funci�n para visualizar el archivo SCR con SDL2
void visualizar_scr(SDL_Renderer *renderer, unsigned char* scr_data) {
    int x, y;
    for (y = 0; y < HEIGHT; y++) {
        for (x = 0; x < WIDTH; x++) {
            int byte_pos = get_pixel_address(x, y);//((y * WIDTH + x) / 8); // Determinamos la posici�n del byte correspondiente
            int bit_pos = (y * WIDTH + x) % 8;  // Determinamos el bit en el byte
            //int _bit_is_set = (scr_data[byte_pos+6144] >> (7 - bit_pos)) & 1; // Extraemos el color (bit 0 o 1)
			int _bit_is_set = ((scr_data[byte_pos]) >> (7 - (x%8))) & 1;
			int _attr = scr_data[get_attribute_address(x, y)+6144];
			int _ink = (int) (_attr & 0b0111);
			int _paper = ((_attr & 0x38) /8);//(int)((_attr >> 3) & 0b0111);
			
			if (_switch_BW)
			{
				_ink=0;
				_paper=7;
			}
			

			int color_index = _bit_is_set ? _ink : _paper;

            // Seleccionamos el color de la paleta
            Uint32 color = palette[color_index];
            SDL_SetRenderDrawColor(renderer, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }
}

Uint32 get_pixel32( SDL_Surface *surface, int x, int y )
{
    //Convert the pixels to 32 bit
    Uint32 *pixels = (Uint32 *)surface->pixels;
    
    //Get the requested pixel
    return pixels[ ( y * surface->w ) + x ];
}

void put_pixel32( SDL_Surface *surface, int x, int y, Uint32 pixel )
{
    //Convert the pixels to 32 bit
    Uint32 *pixels = (Uint32 *)surface->pixels;
    
    //Set the pixel
    pixels[ ( y * surface->w ) + x ] = pixel;
}

void handleInput(SDL_Event event) {
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                // Handle up key
				_SCALE+=0.5;
				SDL_RenderSetScale(renderer, _SCALE, _SCALE);
				SDL_SetWindowSize(window, WIDTH*_SCALE, HEIGHT*_SCALE);
                break;
            case SDLK_DOWN:
                // Handle down key
				_SCALE-=0.5;
				SDL_RenderSetScale(renderer, _SCALE, _SCALE);
				SDL_SetWindowSize(window, WIDTH*_SCALE, HEIGHT*_SCALE);
                break;
            case SDLK_LEFT:
                // Handle left key
				_switch_BW = 0;
                break;
            case SDLK_RIGHT:
                // Handle right key
				_switch_BW = 1;
                break;
            case SDLK_ESCAPE:
                // Exit the emulator
                running = 0;
                break;
            // Add more key handling as needed
			case SDLK_s: // Save state
                //saveState("savestate.dat");
                break;
            case SDLK_l: // Load state
                //loadState("savestate.dat");
                break;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s <archivo.scr>\n", argv[0]);
        return 1;
    }

    // Inicializar SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Error de inicializacion de SDL: %s\n", SDL_GetError());
        return 1;
    }

    // Crear ventana y renderer
    window = SDL_CreateWindow("Visor de SCR ZX Spectrum",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          WIDTH*_SCALE, HEIGHT*_SCALE, SDL_WINDOW_RESIZABLE/*SDL_WINDOW_SHOWN*/);

	//Get window surface
	//screenSurface = SDL_GetWindowSurface( window );

    if (!window) {
        printf("Error al crear la ventana: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Error al crear el renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

	SDL_RenderSetScale(renderer, _SCALE, _SCALE);

    // Cargar el archivo .scr
    unsigned char* scr_data = cargar_scr(argv[1]);
    if (!scr_data) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

   

	//Fill the surface white
	//SDL_FillRect( screenSurface, NULL, SDL_MapRGB( screenSurface->format, 0xFF, 0xFF, 0xFF ) );
	/*for (int _i=0 ; _i<25 ; _i++ )
	{
		put_pixel32(screenSurface, _i, 0, 0xff0000);
	}*/
	
	

	
	//Update the surface
	//SDL_UpdateWindowSurface( window );

    // Esperar a que el usuario cierre la ventana
    SDL_Event event;
    running = 1;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
			handleInput(event);

			 
			
        }
		// Mostrar la imagen del SCR en la ventana
			SDL_RenderClear(renderer);
			visualizar_scr(renderer, scr_data);
			SDL_RenderPresent(renderer);
    }

    // Limpiar recursos
    free(scr_data);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
