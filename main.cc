#include "SDL.h"
#include "SDL_ttf.h"

#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>

using std::string;
using std::vector;

int findWord (const std::string str, const std::string substr, int curr = 0)
{
    int lastIndex = curr, found = true;

    for (int i = curr, j = 0; i < str.length () && j < substr.length ();
         i++, j++)
    {
        if (str[i] == substr[0])
            lastIndex = i;

        if (str[i] != substr[j])
        {
            found = false;

            if (lastIndex != curr)
                return findWord (str, substr, lastIndex);
            else
                for (; i < str.length (); i++)
                    if (str[i] == substr[0])
                        return findWord (str, substr, i);
        }
    }

    return (found) ? curr : -1;
}

string findMacAddr (string str, int *lastIndex = nullptr)
{
    string result = "";

    for (int i = 0; i < str.length (); i++)
    {
        if (str[i] == ' ')
        {
            if (findWord (result, ":") >= 0)
            {
                if (lastIndex)
                    *lastIndex = i + 1;

                return result;
            }
            else
            {
                result = "";
            }
        }
        else if ((str[i] >= '0' && str[i] <= '9')
                 || (str[i] >= 'a' && str[i] <= 'f')
                 || (str[i] >= 'A' && str[i] <= 'F') || str[i] == ':')
        {
            result += str[i];
        }
    }

    return result;
}

char currSymbol = 'a';

struct Entry
{
    char   symbol;
    string macAddr, name;
};

TTF_Font             *font;
vector<SDL_Texture *> textures (1000);

SDL_Texture *glyph (SDL_Renderer *renderer, Uint32 c)
{
    if (c > textures.capacity ())
        return textures[32];
    else if (textures[c])
        return textures[c];

    SDL_Surface *surface
        = TTF_RenderGlyph32_Blended (font, c, { 255, 255, 255, 255 });

    if (!surface)
        return NULL;

    textures[c] = SDL_CreateTextureFromSurface (renderer, surface);

    SDL_SetTextureBlendMode (textures[c], SDL_BLENDMODE_BLEND);

    if (!textures[c])
        return NULL;

    return textures[c];
}

void parseOutput (vector<Entry> &entries, vector<string> &devices)
{
    char           currSymbol = 'a';
    vector<string> newDevices { "" };

    FILE *handle = popen ("bluetoothctl devices", "r");

    char buff;

    while (read (fileno (handle), &buff, 1) > 0)
    {
        if (buff == '\n')
            newDevices.push_back (string { "" });
        else
            newDevices.back () += buff;
    }

    pclose (handle);

    if (newDevices.back ().length () == 0)
        newDevices.pop_back ();

    for (auto s : newDevices)
    {
        bool found = false;

        for (auto s2 : devices)
        {
            if (s == s2)
            {
                found = true;
                break;
            }
        }

        if (!found)
            devices.push_back (s);
    }

    entries.clear ();

    for (auto s : devices)
    {
        int    deviceName = -1;
        string macAddr    = findMacAddr (s, &deviceName);
        string devName    = s.substr (deviceName);

        entries.push_back (Entry {
            currSymbol++,
            macAddr,
            devName,
        });
    }
}

vector<string> messageOutput (string option, string macAddr)
{
    char           buff;
    string         result = "bluetoothctl " + option + " " + macAddr;
    vector<string> message { "" };

    FILE *thing = popen (result.c_str (), "r");

    while (read (fileno (thing), &buff, 1) > 0)
    {
        if (buff == '\n')
            message.push_back ("");
        else
            message.back () += buff;
    }

    pclose (thing);

    return message;
}

int fontW, fontH;

struct Panel
{
    SDL_Point cursor;
    SDL_Rect  region;
    SDL_Color border, hl;

    SDL_Point cursorOffset;

    void inc (int size)
    {
        if (cursor.y < size - 1)
            cursor.y++;
    }

    void dec ()
    {
        if (cursor.y * fontH > region.y)
            cursor.y--;
    }

    void draw (vector<string> entries, SDL_Renderer *renderer)
    {
        SDL_Rect hlRect = {
            region.x,
            region.y + (cursor.y * fontH),
            region.w,
            fontH,
        };

        SDL_Rect rect = { region.x, region.y, fontW, fontH };

        SDL_SetRenderDrawColor (renderer, hl.r, hl.g, hl.b, hl.a);
        SDL_RenderFillRect (renderer, &hlRect);
        SDL_RenderDrawRect (renderer, &region);

        for (auto e : entries)
        {
            for (auto s : e)
            {
                SDL_RenderCopy (renderer, glyph (renderer, s), NULL, &rect);
                rect.x += fontW;
            }

            rect.x = region.x;
            rect.y += fontH;
        }
    }
};

TTF_Font *getFont (int size)
{
    TTF_Font *font = TTF_OpenFont (
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf", size);

    TTF_SizeText (font, "a", &fontW, &fontH);

    return font;
}

int scanOnFunction (void *data)
{
    system ("bluetoothctl scan on");

    return 0;
}

int main (int argc, char **argv)
{
    for (int i = 0; i < textures.size (); i++)
        textures[i] = nullptr;

    SDL_Init (SDL_INIT_EVERYTHING);

    TTF_Init ();

    SDL_Window *window = SDL_CreateWindow ("bluetoothclient", 0, 0, 640, 360,
                                           SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_RenderSetLogicalSize (renderer, 640, 360);

    int fontSize = 12;
    font         = getFont (fontSize);

    bool      run = true;
    SDL_Event event;

    vector<Entry>  entries;
    vector<string> devices, message;

    parseOutput (entries, devices);

    vector<string> options = {
        "connect", "disconnect", "remove", "trust", "pair",
    };

    enum Section
    {
        DEVICE,
        OPTION,
    };

    Section section = DEVICE;

    Panel devicePanel = {
        { 0, 0 },
        { 0, 0, 480, 180 },
        { 255, 255, 255, 255 },
        { 128, 64, 10, 100 },
    };

    Panel optionsPanel = {
        { 0, 0 },
        { 480, 0, 160, 180 },
        { 255, 255, 255, 255 },
        { 64, 128, 10, 100 },
    };

    Panel messagePanel = {
        { 0, 0 },
        { 0, 180, 640, 180 },
        { 255, 255, 255, 255 },
        { 10, 128, 128, 100 },
    };

    SDL_SetWindowPosition (window, 640, 30);

    SDL_Thread *scanOnThread
        = SDL_CreateThread (scanOnFunction, "scanOnThread", nullptr);

    Uint32 currTime = SDL_GetTicks ();

    while (run)
    {
        if (SDL_GetTicks () - currTime > 1000)
        {
            parseOutput (entries, devices);
            currTime = SDL_GetTicks ();
        }

        while (SDL_PollEvent (&event))
        {
            switch (event.type)
            {
                case SDL_QUIT: run = false; break;
                case SDL_TEXTINPUT:
                    if (event.text.text[0] == '=')
                    {
                        free (font);
                        font = getFont (++fontSize);
                    }

                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym)
                    {
                        case SDLK_j:
                            switch (section)
                            {
                                case DEVICE:
                                    devicePanel.inc (devices.size ());
                                    break;
                                case OPTION:
                                    optionsPanel.inc (options.size ());
                                    break;
                            }
                            break;
                        case SDLK_k:
                            switch (section)
                            {
                                case DEVICE: devicePanel.dec (); break;
                                case OPTION: optionsPanel.dec (); break;
                            }
                            break;
                        case SDLK_h:
                            section               = DEVICE;
                            optionsPanel.cursor.y = 0;
                            break;

                        case SDLK_RETURN:
                            if (section == DEVICE)
                                section = OPTION;
                            else if (section == OPTION)
                                message = messageOutput (
                                    options[optionsPanel.cursor.y],
                                    entries[devicePanel.cursor.y].macAddr);

                            break;

                        case SDLK_l:
                            if (section == DEVICE)
                                section = OPTION;
                            else if (section == OPTION)
                                message = messageOutput (
                                    options[optionsPanel.cursor.y],
                                    entries[devicePanel.cursor.y].macAddr);

                            break;
                    }

                    break;
            }
        }

        SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
        SDL_RenderClear (renderer);

        devicePanel.draw (devices, renderer);

        if (section == OPTION)
            optionsPanel.draw (options, renderer);

        if (message.size ())
            messagePanel.draw (message, renderer);

        SDL_RenderPresent (renderer);
    }

    SDL_Quit ();

    return 0;
}
