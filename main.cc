#include "SDL.h"
#include "SDL_ttf.h"

#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>

#include <string>
#include <cassert>

using std::string;
using std::vector;

#define sdlerr(Data)                                            \
    if (Data == NULL)                                           \
    {                                                           \
        std::cout << "ERROR: " << SDL_GetError () << std::endl; \
        exit (1);                                               \
    }

vector<string> runCmd (const string cmd, const string op)
{
    vector<string> result { "" };
    char           buffer;
    FILE          *pipe = popen (cmd.c_str (), op.c_str ());

    while (read (fileno (pipe), &buffer, 1) > 0)
        if (buffer == '\n')
            result.push_back ("");
        else
            result.back () += buffer;

    pclose (pipe);

    return result;
}

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
    vector<string> newDevices = runCmd ("bluetoothctl devices", "r");

    if (newDevices.back ().length () == 0)
        newDevices.pop_back ();

    for (auto &s : newDevices)
    {
        bool found = false;

        s = s.substr (string ("Device ").length ());

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

        s = (!found) ? devices.erase (s) : s + 1;
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
    return runCmd ("bluetoothctl " + option + " " + macAddr, "r");
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

SDL_cond  *cond;
SDL_mutex *mutex;

int scanOnFunction (void *data)
{
    SDL_LockMutex (mutex);

    string pid   = "btcl" + std::to_string (getpid ()),
           cmd   = "exec -a '" + pid + "' bluetoothctl scan on",
           pgrep = "pgrep -f " + pid;

    popen (cmd.c_str (), "r");

    vector<string> pids = runCmd (pgrep.c_str (), "r");

    if (pids.back ().length () == 0)
        pids.pop_back ();

    string message = "";

    for (auto str : pids)
        message += "kill " + str + ";";

    SDL_CondWait (cond, mutex);

    if (message.length () > 0)
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

string getHome ()
{
    vector<string> result = runCmd ("echo $HOME", "r");

    assert (result[0].length () > 0);

    return result[0];
}

struct Config
{
    string path, fontPath;
    int    fontSize, wX, wY;

    void save ()
    {
        string toWrite = std::to_string (fontSize) + "\n" + fontPath + "\n"
                         + std::to_string (wX) + "\n" + std::to_string (wY)
                         + "\n";

        SDL_RWops *rw = SDL_RWFromFile (path.c_str (), "w");

        assert (rw != NULL);

        SDL_RWwrite (rw, toWrite.c_str (), sizeof (char) * toWrite.length (),
                     1);

        SDL_RWclose (rw);
    }

    TTF_Font *getFont ()
    {
        TTF_Font *font = TTF_OpenFont (fontPath.c_str (), fontSize);

        sdlerr (font);

        TTF_SizeText (font, "a", &fontW, &fontH);

        return font;
    }
};

int main (int argc, char **argv)
{
    string HOMEDIR = getHome ();

    Config config {
        HOMEDIR + "/.config/vitoothconfig",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
        12,
        0,
        0,
    };

    for (int i = 0; i < textures.size (); i++)
        textures[i] = nullptr;

    SDL_Init (SDL_INIT_EVERYTHING);
    TTF_Init ();
    cond  = SDL_CreateCond ();
    mutex = SDL_CreateMutex ();

    SDL_Window *window = SDL_CreateWindow ("bluetoothclient", 0, 0, 640, 360,
                                           SDL_WINDOW_SHOWN);

    sdlerr (window);

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    sdlerr (renderer);

    SDL_RenderSetLogicalSize (renderer, 640, 360);

    int linum = 0;

    const char *configRaw = fileRead (config.path.c_str ());

    if (configRaw == NULL)
    {
        system (string ("touch " + config.path).c_str ());
    }
    else
    {
        for (int i = 0; configRaw[i]; i++)
        {
            string result = "";

            while (configRaw[i] != '\n' && configRaw[i])
                result += configRaw[i++];

            if (linum != 1)
            {
                int conv = std::stoi (result);

                if (linum == 0)
                    config.fontSize = conv;
                else if (linum == 2)
                    config.wX = conv;
                else if (linum == 3)
                    config.wY = conv;
            }
            else
            {
                config.fontPath = result;
            }

            linum++;
        }
    }

    font = config.getFont ();

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

    SDL_SetWindowPosition (window, config.wX, config.wY);

    SDL_Thread *scanOnThread
        = SDL_CreateThread (scanOnFunction, "scanOnThread", nullptr);

    Uint32 currTime = SDL_GetTicks ();

    while (run)
    {
        if (SDL_GetTicks () - currTime > 1000)
        {
            parseOutput (entries, devices);
            currTime = SDL_GetTicks ();

            if (devices.size () > 0)
            {
                if (devicePanel.cursor.y > devices.size () - 1)
                    devicePanel.cursor.y = devices.size () - 1;
            }
            else
            {
                devicePanel.cursor.y = 0;
            }
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
                            SDL_GetWindowPosition (window, &config.wX,
                                                   &config.wY);

                            config.save ();

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
                            config.fontSize++;

                            font = config.getFont ();

                            config.save ();

                            for (int i = 0; i < textures.size (); i++)
                                textures[i] = nullptr;

                            break;
                        case SDLK_MINUS:
                            if (config.fontSize < 8)
                                break;

                            free (font);
                            config.fontSize--;
                            font = config.getFont ();

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
