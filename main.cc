#include "SDL.h"
#include "SDL_ttf.h"

#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>

#include <string>

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

    for (auto s = devices.begin (); s != devices.end ();)
    {
        bool found = false;

        for (auto s2 : newDevices)
        {
            if (*s == s2)
            {
                found = true;
                break;
            }
        }

        if (!found)
            s = devices.erase (s);
        else
            s++;
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
                if (rect.x > (region.x + region.w) - fontW)
                    break;
                SDL_RenderCopy (renderer, glyph (renderer, s), NULL, &rect);
                rect.x += fontW;
            }

            rect.x = region.x;
            rect.y += fontH;

            if (rect.y > (region.y + region.h) - fontH)
                break;
        }
    }
};

TTF_Font *getFont (string fontPath, int size)
{
    TTF_Font *font = TTF_OpenFont (fontPath.c_str (), size);
    TTF_SizeText (font, "a", &fontW, &fontH);
    return font;
}

SDL_cond  *cond;
SDL_mutex *mutex;

int scanOnFunction (void *data)
{
    SDL_LockMutex (mutex);

    char buffer;

    string pid   = "btcl" + std::to_string (getpid ()),
           cmd   = "exec -a '" + pid + "' bluetoothctl scan on",
           pgrep = "pgrep -f " + pid;

    popen (cmd.c_str (), "r");

    FILE  *pipe    = popen (pgrep.c_str (), "r");
    string message = "kill ";

    while (read (fileno (pipe), &buffer, 1) > 0)
        message += buffer;

    pclose (pipe);

    if (message.length () < 1)
        exit (1);

    SDL_CondWait (cond, mutex);

    system (message.c_str ());

    return 0;
}

char *fileRead (const char *filename)
{
    SDL_RWops *rw = SDL_RWFromFile (filename, "rb");
    if (rw == NULL)
        return NULL;

    Sint64 res_size = SDL_RWsize (rw);
    char  *res      = (char *)malloc (res_size + 1);

    Sint64 nb_read_total = 0, nb_read = 1;
    char  *buf = res;
    while (nb_read_total < res_size && nb_read != 0)
    {
        nb_read = SDL_RWread (rw, buf, 1, (res_size - nb_read_total));
        nb_read_total += nb_read;
        buf += nb_read;
    }
    SDL_RWclose (rw);
    if (nb_read_total != res_size)
    {
        free (res);
        return NULL;
    }

    res[nb_read_total] = '\0';
    return res;
}

void saveConfig (string fontPath, int fontSize, int wX, int wY)
{
    string toWrite = std::to_string (fontSize) + "\n" + fontPath + "\n"
                     + std::to_string (wX) + "\n" + std::to_string (wY) + "\n";

    SDL_RWops *rw = SDL_RWFromFile ("config", "w");

    if (!rw)
        return;

    SDL_RWwrite (rw, toWrite.c_str (), sizeof (char) * toWrite.length (), 1);

    SDL_RWclose (rw);
}

int main (int argc, char **argv)
{
    for (int i = 0; i < textures.size (); i++)
        textures[i] = nullptr;

    SDL_Init (SDL_INIT_EVERYTHING);

    TTF_Init ();

    cond  = SDL_CreateCond ();
    mutex = SDL_CreateMutex ();

    SDL_Window *window = SDL_CreateWindow ("bluetoothclient", 0, 0, 640, 360,
                                           SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_RenderSetLogicalSize (renderer, 640, 360);

    int         linum = 0, fontSize = 12, wX = 0, wY = 0, offset = 0;
    string      fontPath  = "";
    const char *configRaw = fileRead ("config");

    for (int i = 0; configRaw[i]; i++)
    {
        string result = "";

        while (configRaw[i] != '\n' && configRaw[i])
            result += configRaw[i++];

        std::cout << result << std::endl;

        if (linum != 1)
        {
            int conv = std::stoi (result);

            if (linum == 0)
                fontSize = conv;
            else if (linum == 2)
                wX = conv;
            else if (linum == 3)
                wY = conv;
        }
        else
        {
            fontPath = result;
        }

        linum++;
        offset = i;
    }

    font = getFont (fontPath, fontSize);

    bool           run = true;
    SDL_Event      event;
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
        { 0, 0 },
    };

    Panel optionsPanel = {
        { 0, 0 },
        { 480, 0, 160, 180 },
        { 255, 255, 255, 255 },
        { 64, 128, 10, 100 },
        { 0, 0 },
    };

    Panel messagePanel = {
        { 0, 0 },
        { 0, 180, 640, 180 },
        { 255, 255, 255, 255 },
        { 10, 128, 128, 100 },
        { 0, 0 },
    };

    SDL_SetWindowPosition (window, wX, wY);

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
                case SDL_WINDOWEVENT:
                    switch (event.window.event)
                    {
                        case SDL_WINDOWEVENT_MOVED:
                            SDL_GetWindowPosition (window, &wX, &wY);

                            saveConfig (fontPath, fontSize, wX, wY);

                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym)
                    {
                        case SDLK_q: run = false; break;
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
                            {
                                message = messageOutput (
                                    options[optionsPanel.cursor.y],
                                    entries[devicePanel.cursor.y].macAddr);

                                if (options[optionsPanel.cursor.y] == "remove")
                                {
                                    section              = DEVICE;
                                    devicePanel.cursor.y = optionsPanel.cursor.y
                                        = 0;
                                }
                            }
                            break;
                        case SDLK_l:
                            if (section == DEVICE)
                                section = OPTION;
                            else if (section == OPTION)
                            {
                                message = messageOutput (
                                    options[optionsPanel.cursor.y],
                                    entries[devicePanel.cursor.y].macAddr);

                                if (options[optionsPanel.cursor.y] == "remove")
                                {
                                    section              = DEVICE;
                                    devicePanel.cursor.y = optionsPanel.cursor.y
                                        = 0;
                                }
                            }
                            break;
                        case SDLK_EQUALS:
                            free (font);
                            font = getFont (fontPath, ++fontSize);

                            saveConfig (fontPath, fontSize, wX, wY);

                            for (int i = 0; i < textures.size (); i++)
                                textures[i] = nullptr;

                            break;
                        case SDLK_MINUS:
                            if (fontSize < 8)
                                break;

                            free (font);
                            font = getFont (fontPath, --fontSize);

                            saveConfig (fontPath, fontSize, wX, wY);

                            for (int i = 0; i < textures.size (); i++)
                                textures[i] = nullptr;

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

    SDL_CondSignal (cond);
    SDL_WaitThread (scanOnThread, nullptr);

    SDL_Quit ();

    return 0;
}
